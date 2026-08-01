# rForth syntax highlighting for VS Code

Highlighting for rForth sources (`.fs`, `.fth`, `.4th`, `.forth`): a TextMate
grammar plus a semantic pass that resolves locals and user-defined words.

Unlike generic Forth grammars, it follows what the interpreter actually accepts:

- the word list is generated straight from the `CODE`/`IMMD`/`COMP`/`ICOMP`
  macros in `src/rForth.cpp`, so only built-in words are highlighted
  (case-sensitive, matching `CASE_SENSITIVE=1`); words defined in Forth stay
  plain, except at their definition site
- Forth tokenizing rules — words are delimited by whitespace only, so `\` is a
  comment only as a standalone token, `(` only opens a comment when followed by
  a space, and `0<>`, `f.r`, `1/f`, `-rot` stay single words
- rForth locals: `{: a b -- c :}`, `f{: x -- y f:}`, `-> name`, `f-> name`
- strings and printing words: `s" …"`, `." …"`, `abort" …"`, `.( …)` — the
  introducing word is scoped as string punctuation, so the whole construct is
  one colour
- defining words name their word: `: foo`, `create buf`, `constant N`,
  `variable v`, `mutex m`, `task t`, `' word`, `['] word`
- number literals including `$hex`, `%binary`, `#`/`&` decimal, floats
  (`1.5`, `1e3`) and the trailing-dot double form

## Semantic highlighting

A grammar can only colour a name where it is written, so `accepted` would be
coloured in `{: -- accepted :}` and after `-> accepted`, but not where it is
read. `parser.js` closes that gap: it walks the file the way the interpreter
does — skipping comments and strings, tracking the current `:` definition — and
reports every *read* of a local as `parameter`. Locals do not leak past `;`.

Words defined in the file are not touched: like the built-ins, they are
highlighted where they are defined and stay plain where they are called.

This needs semantic highlighting enabled, which is the default for themes that
support it (`"editor.semanticHighlighting.enabled": "configuredByTheme"`).

## Install

Link the extension into VS Code and restart it:

```sh
ln -s "$PWD/editors/vscode" ~/.vscode/extensions/rforth
```

`.fs` is also claimed by F# and by generic Forth extensions. If highlighting
does not switch, pin the language in the workspace `.vscode/settings.json`:

```json
{ "files.associations": { "*.fs": "rforth" } }
```

## Regenerating the grammar

After adding, renaming or removing dictionary words:

```sh
python3 editors/vscode/generate-grammar.py   # rewrites syntaxes/rforth.tmLanguage.json
```

There are no npm dependencies and no build step. To see why something is
coloured the way it is, use `Developer: Inspect Editor Tokens and Scopes` in
the editor.
