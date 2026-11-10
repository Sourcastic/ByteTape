# ByteTape

A language for simulating Turing machines.

**Authors**

| Name | Roll number |
|---|---|
| Ibrahim Faisal | 23L-0759 |
| Atika Hussain | 23L-0570 |
| Eshaal Rehmatullah | 23L-0648 |

---

## 1. Installation

### 1.1 Dependencies

You need `gcc`, `make`, `flex`, `bison`, `clang` and `llvm`.

| Distribution | Command |
|---|---|
| Ubuntu / Debian | `sudo apt install gcc make flex bison clang llvm` |
| Fedora | `sudo dnf install gcc make flex bison clang llvm` |
| Arch | `sudo pacman -S gcc make flex bison clang llvm` |

### 1.2 Build

```sh
make
```

This builds the interpreter `bytetape` and the runtime library `libbtrt.a`.

---

## 2. Usage

The project provides two commands:

- `./bytetape`: the interpreter.
- `./bytetapec`: the ahead-of-time compiler.

### 2.1 Interpreter

```sh
./bytetape [options] program.bt
```

| Option | Description |
|---|---|
| `-q` | Quiet: do not print the final tapes when the program stops. |
| `--trace` | Print the state and every tape before each step. |
| `--max-steps N` | Stop with an error after `N` steps (catches endless loops). |
| `--strict` | Accept only formal Turing machines (see [Strict mode](#315-strict-mode)). |
| `--emit-ll FILE` | Do not run: compile to LLVM IR and write it to `FILE` (`-` means stdout). |
| `-h`, `--help` | Show the help message. |

**Exit codes**

| Code | Meaning |
|---|---|
| 0 | Halt or accept |
| 1 | Reject |
| 2 | Usage error |
| 3 | Compile error |
| 4 | Runtime error |

### 2.2 Ahead-of-time compiler

```sh
./bytetapec [-S] [--strict] [-O0|-O1|-O2|-O3] program.bt [-o output]
```

| Option | Description |
|---|---|
| `-S` | Stop after generating LLVM IR. |
| `--strict` | Accept only formal Turing machines (see [Strict mode](#315-strict-mode)). |
| `-O0`, `-O1`, `-O2`, `-O3` | Clang optimization level (default `-O2`). |
| `-o output` | Name of the output file. |
| `-h`, `--help` | Show the help message. |

---

## 3. Syntax

### 3.1 Program layout

A program has up to three sections, in this order. `%% tapes` and `%% states` are required. `%% alphabet` is optional and must come first.

```
%% alphabet
(optional: input and tape symbols)

%% tapes
(tape declarations)

%% states
(state definitions)
```

### 3.2 Alphabet (optional)

```
%% alphabet
input: {'a', 'b'}
tape:  {'X', 'Y'}
```

| Line | Meaning |
|---|---|
| `input: {...}` | The input alphabet (Σ): the symbols the initial tape contents may use. |
| `tape: {...}` | Extra working symbols the machine may write while running. |

- The tape alphabet (Γ) is the input symbols, the working symbols, and the blank. The blank is always in Γ.
- Both lines are optional, and each may appear at most once. No other name is allowed in this section.
- The blank (`_`) cannot be an input symbol.
- Listing a symbol twice gives a warning.
- If the section is left out, every value from 0 to 255 is allowed.

When an alphabet is declared, these rules are enforced:

| Rule | Error |
|---|---|
| Initial tape contents must be input symbols (and not blank). | Compile error (exit 3) |
| Literals in `write`, in conditions, and in `on` patterns must be in Γ. The `*` pattern is exempt. | Compile error (exit 3) |
| A value produced by `inc`, `dec` or `input` must be in Γ. | Runtime error (exit 4) |

Copying a cell with `write [A] [B]` is not checked.

### 3.3 Comments

- `#` starts a comment that runs to the end of the line.
- Whitespace and newlines have no meaning.

### 3.4 Tape declarations

| Syntax | Meaning |
|---|---|
| `Name: {a, b, c}` | Declares a tape with values in cells 0, 1, 2. The head starts on cell 0. |
| `Name: {}` | Declares an empty tape (all blank). |

If an alphabet is declared, every initial value must be an input symbol.

### 3.5 Values

| Kind | Syntax |
|---|---|
| Integer literal | `0` to `255` |
| Character literal | `'a'`, with escapes `'\n'` `'\t'` `'\r'` `'\0'` `'\\'` `'\''` |
| Blank | `_` (the same as 0) |

### 3.6 Cells

| Syntax | Meaning |
|---|---|
| `[Tape]` | The cell under the head of `Tape`. |
| `[Tape + 1]` | One cell to the right of the head. |
| `[Tape - 2]` | Two cells to the left of the head. |
| `Tape` | The tape itself (used only by `move`). |

The offset must be an integer literal.

### 3.7 States

| Syntax | Meaning |
|---|---|
| `state Name { statements }` | Defines a state. |
| `start state Name { statements }` | Defines the state where execution begins. |

If no state is marked `start`, the first state in the file is used. At most one state may be marked `start`.

### 3.8 Statements

| Statement | Meaning |
|---|---|
| `write [Cell] value` | Stores a literal or a cell value in a cell. |
| `inc [Cell]` | Adds 1, wrapping from 255 to 0. |
| `dec [Cell]` | Subtracts 1, wrapping from 0 to 255. |
| `move Tape L` | Moves the head one cell left. |
| `move Tape R` | Moves the head one cell right. |
| `print [Cell]` | Outputs the cell's value as a character. |
| `printn [Cell]` | Outputs the cell's value as a decimal number followed by a newline. |
| `input [Cell]` | Reads one byte from standard input into the cell (blank at end of input). |
| `transition Name` | Ends the state and continues in state `Name`. |
| `halt` | Stops the machine. |
| `accept` | Stops the machine with the verdict ACCEPT. |
| `reject` | Stops the machine with the verdict REJECT. |

`if` and `on` are described in the next two sections.

### 3.9 If

```
if condition { statements }
if condition { statements } else { statements }
if condition { statements } else if condition { statements }
```

### 3.10 Conditions

- A comparison has two operands (literals or cells) and one of: `==`, `!=`, `<`, `>`, `<=`, `>=`.
- `and`, `or` and `not` combine conditions.
- Parentheses group conditions.
- If an alphabet is declared, literal operands must be in Γ.
- `not` binds tightest, then `and`, then `or`.

Example:

```
if [A] < [B] and not [C] == '$' { ... }
```

### 3.11 On (rule table)

```
on [Tape], [Tape + 1] {
    pattern, pattern -> { statements }
    pattern, pattern -> { statements }
}
```

- The cells after `on` are read once, before any row runs.
- A pattern is a value (`5`, `'x'`, `_`) or `*`, which matches anything. If an alphabet is declared, values must be in Γ.
- Each row has exactly one pattern per listed cell (a compile error otherwise). At most 16 cells may be listed.
- Rows are tried from top to bottom, and the first match wins. A row that an earlier row already covers gives a warning.
- If no row matches, the program stops with a runtime error.
- If the chosen row ends without `transition`, `halt`, `accept` or `reject`, execution continues after the `on`.

### 3.12 Reserved words

These cannot be used as tape or state names:

```
state start transition halt accept reject write move inc dec
if else on and or not print printn input L R
```

Names are case-sensitive. The tape `Input` is allowed, but `input` is the statement.

### 3.13 Minimal example

```
%% tapes
Bits: {1, 1, 0, 1}

%% states
state Inc {
    on [Bits] {
        _ -> { write [Bits] 1  halt }
        1 -> { write [Bits] 0  move Bits R  transition Inc }
    }
}
```

### 3.14 Example with an alphabet

Decides whether a word has the form a^n b^n (see `examples/anbn.bt`).

```
%% alphabet
input: {'a', 'b'}
tape:  {'X', 'Y'}

%% tapes
T: {'a', 'a', 'b', 'b'}

%% states
state Start {
    on [T] {
        'a' -> { write [T] 'X'  move T R  transition FindB }
        'Y' -> { transition CheckY }
        _   -> { accept }
        *   -> { reject }
    }
}

state FindB {
    on [T] {
        'a' -> { move T R  transition FindB }
        'Y' -> { move T R  transition FindB }
        'b' -> { write [T] 'Y'  move T L  transition Back }
        *   -> { reject }
    }
}

state Back {
    on [T] {
        'X' -> { move T R  transition Start }
        *   -> { move T L  transition Back }
    }
}

state CheckY {
    on [T] {
        'Y' -> { move T R  transition CheckY }
        _   -> { accept }
        *   -> { reject }
    }
}
```

### 3.15 Strict mode

`--strict` accepts only programs that are formal Turing machines: a deterministic transition
function over k tapes. Anything else is a compile error (exit code 3). A program that passes
runs exactly as it would without the flag.

```sh
./bytetape --strict examples/anbn.bt
./bytetapec --strict examples/palindrome.bt
```

A strict program must follow these rules:

| # | Rule |
|---|---|
| 1 | It has an `%% alphabet` section and at least one tape. |
| 2 | Only the first tape holds the input. Every other tape starts blank (`{}`). |
| 3 | Every state is exactly one `on` table and nothing else. |
| 4 | The `on` lists every tape exactly once, at offset 0, for example `on [A], [B]`. |
| 5 | Every row is writes, then moves, then one of `transition`, `accept`, `reject` or `halt`. |
| 6 | The table covers every combination of symbols in Γ, and no row is dead. |
| 7 | No `if`, `inc`, `dec`, `print`, `printn`, `input`, offsets, or cell-to-cell `write`. |

Details:

- A `write` must store a symbol literal, at most once per tape. Leaving a tape out means "write back the same symbol".
- A `move` is `L` or `R`, at most once per tape. Leaving a tape out means "stay".
- All writes come before all moves, because a write always goes to the cell the head was on.
- `*` stands for any symbol of Γ. Rows are still tried top to bottom, so overlapping rows are allowed and a final `*` row works as "otherwise".
- A row that earlier rows already cover is an error in strict mode (a warning otherwise).
- If the table is incomplete, the error lists examples of the missing combinations.
- A strict program can never stop with "no row matches" or "state ended without a transition".

Example error:

```
line 14: error: strict mode: state 'S' does not cover every symbol combination; missing e.g. ('b', _) -- add rows or a final '*' row
```

`anbn.bt`, `increment.bt` and `palindrome.bt` are strict. The other examples are valid ByteTape programs but not Turing machines.

---

## 4. Project files

### 4.1 Layout

```
ByteTape/
├── lexer.l  parser.y  ast.h  ast.c        front end
├── strict.h  strict.c                      --strict checker
├── tape.h  tape.c                          tape runtime
├── interp.h  interp.c                      interpreter
├── codegen.h  codegen.c  rt.c  bytetapec   compiler
├── main.c  Makefile                        entry point and build
├── README.md  SEMANTICS.md                 documentation
├── examples/                               sample programs
└── tests/                                  test programs and test runner
```

### 4.2 Front end (source text to syntax tree)

| File | Description |
|---|---|
| `lexer.l` | The flex tokenizer. Strips comments and whitespace, recognizes the section markers (`%% alphabet`, `%% tapes`, `%% states`), keywords, identifiers, numbers, character literals and escapes, and `_` (blank). Tracks line numbers for error messages. |
| `parser.y` | The bison grammar. Builds the syntax tree and performs the semantic checks: undeclared tapes, duplicate states, values outside 0-255, `on` rows with the wrong number of patterns, unreachable rows, and the alphabet rules. When compiling, it calls the code generator after a successful parse. |
| `ast.h` | Defines the syntax tree node types (operands, conditions, patterns, `on` rows, statements, states). Both the interpreter and the code generator read this tree. Names are turned into integer ids at parse time. |
| `ast.c` | Node constructors and the state table. Also decides the start state (the one marked `start`, otherwise the first in the file). |
| `strict.h` | Interface to the strict-mode checker: the `strict_mode` flag and `strict_check()`. |
| `strict.c` | The `--strict` checker. Walks the finished syntax tree and reports every place where the program is not a formal Turing machine: state shape, `on` listing every tape, row shape, completeness of the table over Γᵏ, and blank non-input tapes. Compile-time only. |

### 4.3 Tape runtime

| File | Description |
|---|---|
| `tape.h` | Interface to the tape table: declaring tapes, reading, writing and moving relative to the head, checking whether a tape is blank, printing tapes, and the alphabet functions. |
| `tape.c` | Implements up to 64 named tapes that are infinite in both directions. Cells are 8-bit values, unwritten cells read as 0, and the array grows on write. Also stores the input and tape alphabets and answers membership checks. Used by both the interpreter and compiled programs. |

### 4.4 Interpreter

| File | Description |
|---|---|
| `interp.h` | Run options (`-q`, `--trace`, `--max-steps`) and the exit codes. |
| `interp.c` | The tree-walking interpreter. Executes statements, evaluates conditions, selects `on` rows, follows transitions, and reports runtime errors, including symbols outside the tape alphabet after `inc`, `dec` and `input`. |

### 4.5 Compiler

| File | Description |
|---|---|
| `codegen.h` | Interface to the LLVM IR generator. |
| `codegen.c` | Turns the syntax tree into textual LLVM IR. Each state becomes a basic block and each `transition` becomes a jump. Emits alphabet checks only when an alphabet is declared. |
| `rt.c` | Runtime support called by compiled programs: tape access wrappers, `print` / `printn` / `input`, runtime error reporting, the alphabet checks, and the final tape dump with ACCEPT or REJECT. Its output and exit codes match the interpreter. |
| `bytetapec` | Shell script that drives compilation: runs `bytetape --emit-ll` to produce LLVM IR, then runs `clang` to link it with `libbtrt.a`. Passes `--strict` through. |

### 4.6 Entry point and build

| File | Description |
|---|---|
| `main.c` | Command-line entry point for `./bytetape`. Parses options (including `--strict`), runs the parser, reports compile errors (exit code 3), then either runs the interpreter or, with `--emit-ll`, writes LLVM IR. |
| `Makefile` | Builds `bytetape` and `libbtrt.a`. Targets: `make` (build), `make test` (run the tests), `make clean` (remove generated files). |

### 4.7 Documentation

| File | Description |
|---|---|
| `README.md` | This file: installation, usage, syntax and file overview. |
| `SEMANTICS.md` | The language specification: program structure, alphabet, tapes and cells, statements, conditions, `on` tables, states, and exit codes. |

### 4.8 Examples (`examples/`)

| File | Description |
|---|---|
| `hello.bt` | Prints a message stored as data on a tape. |
| `cat.bt` | Copies standard input to standard output. |
| `count.bt` | Counts down from 5, printing each value. |
| `myfirst.bt` | Adds 1 to every number on the tape and prints it. |
| `increment.bt` | Adds 1 to a binary number stored least-significant bit first. Strict (a one-tape Turing machine). |
| `palindrome.bt` | Two-tape machine that decides whether a word over {a, b} is a palindrome and prints ACCEPT or REJECT. Strict. |
| `anbn.bt` | Decides the language a^n b^n. Uses the `%% alphabet` section. Strict. |
| `sorter.bt` | Bubble sort over four tapes. |
| `forever.bt` | A machine that never stops, for trying `--max-steps`. |
|  `sorter.bt` | Merge sort. |
