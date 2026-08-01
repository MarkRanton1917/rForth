#!/usr/bin/env python3
"""Generate the rForth TextMate grammar from the interpreter sources.

The word list is extracted from src/rForth.cpp (CODE/IMMD/COMP/ICOMP macros),
so the grammar never drifts away from the built-in dictionary. Words defined in
Forth (forth/memory.fs and any user library) are deliberately left unhighlighted
— they are ordinary definitions, no different from the ones a user writes.
Re-run after adding or renaming words:

    python3 editors/vscode/generate-grammar.py
"""

import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(ROOT, "src", "rForth.cpp")
OUT = os.path.join(HERE, "syntaxes", "rforth.tmLanguage.json")

# Words handled by dedicated patterns instead of the plain word lists.
HANDLED = {
    ":", ";", "(", ".(", "\\", '."', 's"', 'abort"',
    "{:", ":}", "f{:", "f:}", "->", "f->",
    "variable", "constant", "create", "fvariable", "fconstant",
    "2variable", "2constant", "task", "mutex", "forget", "see",
    "'", "[']",
}

CONTROL = [
    "if", "else", "then", "begin", "while", "repeat", "until", "again",
    "do", "?do", "loop", "+loop", "leave", "exit", "execute",
    "catch", "throw", "abort", "boot", "bye", "immediate", "does>",
    "included", "pause", "stop", "resume", "delay", "ms",
]

DEFINING = [
    "variable", "constant", "create", "fvariable", "fconstant",
    "2variable", "2constant", "task", "mutex", "forget", "see",
]


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def core_words():
    """Every word registered through the CODE/IMMD/COMP/ICOMP macros."""
    pattern = re.compile(r'\b(?:CODE|IMMD|COMP|ICOMP)\("((?:\\.|[^"\\])*)"')
    words = set()
    for m in pattern.finditer(read(SRC)):
        words.add(m.group(1).replace('\\"', '"').replace("\\\\", "\\"))
    return words


def alternation(words):
    """Regex alternation, longest first so prefixes never win."""
    return "|".join(re.escape(w) for w in sorted(words, key=lambda w: (-len(w), w)))


# A Forth token is delimited by whitespace only, so every match is fenced by
# these two zero-width guards instead of \b (which would break on ! @ + . etc).
BOL = r"(?<![^\s])"
EOL = r"(?![^\s])"


def word_match(words, scope):
    return {"match": BOL + "(?:" + alternation(words) + ")" + EOL, "name": scope}


def build():
    core = core_words()
    unknown = [w for w in CONTROL + DEFINING if w not in core]
    if unknown:
        raise SystemExit("not in dictionary: " + " ".join(unknown))
    plain = core - HANDLED - set(CONTROL)

    return {
        "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
        "name": "rForth",
        "scopeName": "source.rforth",
        "fileTypes": ["fs", "fth", "4th", "forth"],
        "patterns": [
            {"include": "#comments"},
            {"include": "#strings"},
            {"include": "#colon-definition"},
            {"include": "#defining-words"},
            {"include": "#tick"},
            {"include": "#locals"},
            {"include": "#local-store"},
            {"include": "#control"},
            {"include": "#numbers"},
            {"include": "#core-words"},
        ],
        "repository": {
            "comments": {
                "patterns": [
                    {
                        "match": BOL + r"\\" + EOL + ".*$",
                        "name": "comment.line.backslash.rforth",
                    },
                    {
                        "begin": BOL + r"\(" + EOL,
                        "end": r"\)",
                        "name": "comment.block.parenthesis.rforth",
                        "patterns": [
                            {"match": r"(?<![^\s])--(?![^\s])",
                             "name": "punctuation.separator.stack-effect.rforth"}
                        ],
                    },
                ]
            },
            # The introducing word is scoped as string punctuation, not as a
            # keyword, so `s" text"` reads as one string in every theme.
            "strings": {
                "patterns": [
                    {
                        "begin": BOL + r'(s"|\."|abort")(?=\s|")',
                        "end": r'(")',
                        "beginCaptures": {
                            "1": {"name": "punctuation.definition.string.begin.rforth"}},
                        "endCaptures": {
                            "1": {"name": "punctuation.definition.string.end.rforth"}},
                        "name": "string.quoted.double.rforth",
                    },
                    {
                        "begin": BOL + r"(\.\()",
                        "end": r"(\))",
                        "beginCaptures": {
                            "1": {"name": "punctuation.definition.string.begin.rforth"}},
                        "endCaptures": {
                            "1": {"name": "punctuation.definition.string.end.rforth"}},
                        "name": "string.other.print.rforth",
                    },
                ]
            },
            "colon-definition": {
                "match": BOL + r"(:)\s+(\S+)",
                "captures": {
                    "1": {"name": "keyword.control.definition.rforth"},
                    "2": {"name": "entity.name.function.rforth"},
                },
            },
            "defining-words": {
                "match": BOL + "(" + alternation(DEFINING) + r")\s+(\S+)",
                "captures": {
                    "1": {"name": "storage.type.rforth"},
                    "2": {"name": "entity.name.function.rforth"},
                },
            },
            "tick": {
                "match": BOL + r"('|\['\])\s+(\S+)",
                "captures": {
                    "1": {"name": "keyword.operator.tick.rforth"},
                    "2": {"name": "entity.name.function.rforth"},
                },
            },
            "locals": {
                "begin": BOL + r"(f?\{:)" + EOL,
                "end": BOL + r"(f?:\})" + EOL,
                "beginCaptures": {"1": {"name": "keyword.other.locals.rforth"}},
                "endCaptures": {"1": {"name": "keyword.other.locals.rforth"}},
                "name": "meta.locals.rforth",
                "patterns": [
                    {"match": r"(?<![^\s])--(?![^\s])",
                     "name": "punctuation.separator.stack-effect.rforth"},
                    {"match": r"\S+", "name": "variable.parameter.rforth"},
                ],
            },
            "local-store": {
                "match": BOL + r"(f?->)\s+(\S+)",
                "captures": {
                    "1": {"name": "keyword.operator.assignment.rforth"},
                    "2": {"name": "variable.parameter.rforth"},
                },
            },
            "control": word_match(CONTROL + [";"], "keyword.control.rforth"),
            "numbers": {
                "patterns": [
                    {
                        # a dot or an exponent is what makes it a float literal
                        "match": BOL + r"[+-]?(?:(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+|[eE])?"
                                 + r"|\d+(?:[eE][+-]?\d+|[eE]))" + EOL,
                        "name": "constant.numeric.float.rforth",
                    },
                    {
                        "match": BOL + r"[$][+-]?[0-9a-fA-F]+" + EOL,
                        "name": "constant.numeric.hex.rforth",
                    },
                    {
                        "match": BOL + r"%[+-]?[01]+" + EOL,
                        "name": "constant.numeric.binary.rforth",
                    },
                    {
                        "match": BOL + r"[#&][+-]?\d+" + EOL,
                        "name": "constant.numeric.decimal.rforth",
                    },
                    {
                        "match": BOL + r"[+-]?\d+" + EOL,
                        "name": "constant.numeric.integer.rforth",
                    },
                ]
            },
            "core-words": word_match(plain, "support.function.rforth"),
        },
    }


def main():
    grammar = build()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(grammar, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
