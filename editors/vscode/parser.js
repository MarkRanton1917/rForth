'use strict';

// Scope-aware analysis of an rForth source file. The TextMate grammar can only
// colour a local where it is written (`{: x :}`, `-> x`); this pass follows the
// same rules the interpreter uses, so every *read* of a local is coloured too.

const DEFINING = new Set([
  'variable', 'constant', 'create', 'fvariable', 'fconstant',
  '2variable', '2constant', 'task', 'mutex',
]);

const STRING_WORDS = new Set(['s"', '."', 'abort"']);

const isSpace = (c) => c === ' ' || c === '\t' || c === '\r' || c === '\n';

/**
 * Split the source into whitespace-delimited words, skipping what the
 * interpreter consumes as comments and strings: `\` runs to end of line, `(`
 * and `.(` to the next `)` (across lines), `s" ." abort"` to the next `"`.
 *
 * @returns {{word: string, start: number}[]}
 */
function scan(text) {
  const words = [];
  const n = text.length;
  let i = 0;
  while (i < n) {
    while (i < n && isSpace(text[i])) i++;
    if (i >= n) break;
    const start = i;
    while (i < n && !isSpace(text[i])) i++;
    const word = text.slice(start, i);

    if (word === '\\') {
      while (i < n && text[i] !== '\n') i++;
      continue;
    }
    if (word === '(' || word === '.(') {
      const end = text.indexOf(')', i);
      i = end < 0 ? n : end + 1;
      continue;
    }
    if (STRING_WORDS.has(word)) {
      const end = text.indexOf('"', i);
      i = end < 0 ? n : end + 1;
      continue;
    }
    words.push({ word, start });
  }
  return words;
}

/**
 * Find every read of a local variable — the one thing the grammar cannot see.
 *
 * Words defined in the file are deliberately not reported: they are only
 * highlighted where they are defined, exactly like the built-in dictionary
 * words are the only ones the grammar knows. Declaration sites (`{: x :}`,
 * `-> x`) are covered by the grammar too, so only reads are returned here.
 *
 * @param {string} text
 * @returns {{start: number, length: number, type: 'parameter'}[]}
 */
function analyze(text) {
  const out = [];
  let locals = new Set();
  let pending = null; // the next word is a name, not a use
  let inLocals = false;

  for (const { word, start } of scan(text)) {
    if (inLocals) {
      if (word === ':}' || word === 'f:}') inLocals = false;
      else if (word !== '--') locals.add(word);
      continue;
    }
    if (pending) {
      if (pending === 'store') locals.add(word);
      pending = null;
      continue;
    }
    switch (word) {
    case ':':
      locals = new Set();
      pending = 'name';
      continue;
    case ';':
      locals = new Set();
      continue;
    case '{:':
    case 'f{:':
      inLocals = true;
      continue;
    case '->':
    case 'f->':
      pending = 'store';
      continue;
    case "'":
    case "[']":
    case 'see':
    case 'forget':
      pending = 'name';
      continue;
    default:
      break;
    }
    if (DEFINING.has(word)) {
      pending = 'name';
      continue;
    }

    // A local shadows the dictionary, so no built-in check is needed here.
    if (locals.has(word)) {
      out.push({ start, length: word.length, type: 'parameter' });
    }
  }
  return out;
}

module.exports = { scan, analyze };
