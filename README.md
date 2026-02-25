# tree-sitter-fir

A [tree-sitter] grammar for the [Fir programming language].

This will mainly be used for supporting editors and IDEs that use tree-sitter
grammars for syntax highlighting and other editor features (indentation, tags,
etc.).

[tree-sitter]: https://tree-sitter.github.io/tree-sitter/
[Fir programming language]: https://github.com/fir-lang/fir/

## Development

This grammar is mostly AI-generated from the [reference implementation]'s
[lexer], [scanner] (adds indentation tokens), and [LALRPOP grammar] (LR(1)).

[reference implementation]: https://github.com/fir-lang/fir/blob/d412b0146b539c42991d739ddba2aa0df9d64056/src
[lexer]: https://github.com/fir-lang/fir/blob/d412b0146b539c42991d739ddba2aa0df9d64056/src/lexer.rs
[scanner]: https://github.com/fir-lang/fir/blob/d412b0146b539c42991d739ddba2aa0df9d64056/src/scanner.rs
[LALRPOP grammar]: https://github.com/fir-lang/fir/blob/d412b0146b539c42991d739ddba2aa0df9d64056/src/parser.lalrpop

tree-sitter grammars are extremely difficult to develop and debug, especially
with a complex external scanner like the one we have here to deal with
indentation sensitivity. I recommend always using an AI to make changes, or at
least to debug any unintentional changes.

The scanner and grammar should follow the reference implementation's as much as
possible. We can't completely avoid `conflicts` as we don't want to combine
tokens in this implementation (as queries can only deal with whole tokens), but
we try to keep it as small as possible.

**Testing:** `test.sh` takes a directory with Fir files as argument. It
recursively scans all subdirectories, parses all `.fir` files, and checks for
exit codes.

When run without an argument, it assumes that the Fir checkout is in `../fir`,
and parses all Fir files in the directory. Currently there are a few failures in
`Tool/Format/tests`, caused by discrepancies between the reference
implementation's parser and the compiler's parser. We'll fix those later, for
now ignore those failures.

`test.sh` should not report any parse failures.

**Useful commands:**

- `tree-sitter generate` compiles the grammar to C.
- `tree-sitter parse <file>` parses the file and prints the parse tree.
- `tree-sitter highlight <file>` parses the file and highlights syntax.
