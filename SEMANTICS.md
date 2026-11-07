# ByteTape Semantics

The rules of the language.

## Program structure

A program has two sections, in this order, each introduced by a marker line:

```
# a comment runs from # to the end of the line

%% tapes
Input:  {3, 2, 4, 5, 1, '$'}
Output: {}

%% states
state Start { ... }
start state Other { ... }      # optional: choose where execution begins
```

- `%% tapes` and `%% states` are **section markers**, not comments. Both are required.
- `#` starts a comment.
- Execution begins at the `start state`, or at the **first state in the file** if none is marked.
  At most one state may be marked `start`.
- Whitespace and newlines have no meaning.
- **Reserved words** (cannot be tape or state names): `state start transition halt accept reject
  write move inc dec if else on and or not print printn input L R`. Names are case-sensitive,
  so the tape `Input` is fine but `input` is the statement.

## Tapes and cells

- Every tape is **infinite in both directions**.
- Every cell holds one **8-bit unsigned value (0-255)**. `inc` and `dec` wrap around
  (`255 + 1 = 0`, `0 - 1 = 255`).
- A cell that was never written holds **0**, the **blank**. The literal `_` means blank (0).
- `Name: {a, b, c}` puts the values in cells 0, 1, 2 and starts the head on cell 0.
  `Name: {}` is an empty tape (all blank).
- Literals are integers 0-255, char literals, or `_`. Char literals are `'a'` plus the escapes
  `'\n' '\t' '\r' '\0' '\\' '\''`. A literal outside 0-255 is a compile error.
- Tapes must be declared before the states that use them (the section order guarantees this).

### Brackets mean a cell, a bare name means a tape

| Syntax | Meaning |
|---|---|
| `[Output]` | the cell under Output's head |
| `[Output + 1]` | one cell to the right of the head |
| `[Output - 2]` | two cells to the left of the head |
| `Output` (no brackets) | the tape itself (only `move` uses this) |

The offset must be an integer literal. Reading a cell never moves the head.

## Statements

There is **no expression language**: no arithmetic, no `+ - * /` between values. A statement
looks only at cells and literals.

| Statement | Meaning |
|---|---|
| `write [Dst] x` | store `x` in the cell; `x` is a literal or a cell |
| `inc [Cell]` / `dec [Cell]` | add / subtract 1, wrapping at 8 bits |
| `move Tape L` / `move Tape R` | move that tape's head one cell |
| `print [Cell]` | output the cell's byte as a **character** |
| `printn [Cell]` | output the cell's value as a **decimal number** followed by a newline |
| `input [Cell]` | read one byte from standard input into the cell (blank at end of input) |
| `transition Name` | end this state at once and continue in state `Name` |
| `halt` | stop the machine |
| `accept` / `reject` | stop the machine with a verdict |
| `if cond { ... }` | run the block when `cond` is true (optionally `else { ... }` or `else if ...`) |
| `on cells { rows }` | rule table, see below |

**Output only ever prints cells.** There are no string literals: to print a message, put it
on a tape and walk the head along it (see `examples/hello.bt`).

## Conditions (`if`)

A condition compares **two operands** (literals or cells) with `== != < > <= >=`.
Conditions combine with `and`, `or`, `not` and parentheses. `not` binds tightest, then `and`,
then `or`.

```
if [A] < [B] and not [C] == '$' { ... }
```

## `on`: rule tables

```
on [Tape], [Tape + 1], ... {
    pattern, pattern, ...  ->  { statements }
    pattern, pattern, ...  ->  { statements }
}
```

- Each **pattern** is a value (`5`, `'x'`, `_`) or `*` (matches anything).
- Every row has **exactly one pattern per listed cell** (checked at compile time).
- All listed cells are **read once, before any row runs**. A row that moves a head does not
  change which row was chosen.
- Rows are tried **top to bottom; the first match wins**. Put `*` rows last.
- **No matching row is a runtime error** naming the state and the symbols seen. Add a final
  `*` row for a default.
- The chosen row's block is ordinary statements. If it ends without `transition`, `halt`,
  `accept` or `reject`, execution **continues after the `on`**.
- A row that can never match (an earlier row already covers it) gives a **warning**.
- `if [A] == 'x' { B } else { C }` is shorthand for `on [A] { 'x' -> { B }  * -> { C } }`.
  `if` stays for what `on` cannot say: `<`, `>`, and comparing two cells.

## States

- `state Name { ... }` defines a state. A state may `transition` to a state defined later.
- `transition`, `halt`, `accept` and `reject` end the state **immediately**.
- A state body that runs to the end without one of those is a **runtime error**.
- Using a state name that is never defined, or defining a state twice, is a compile error.

## Running

```
./bytetape [-q] [--trace] [--max-steps N] program.bt
```

- When the machine stops, the final contents of all tapes are printed, e.g. `Output: 2 3 [4] 5`
  (the cell in brackets is under the head; the shown range starts at cell 0). `-q` turns this off.
- `accept` / `reject` also print `ACCEPT` / `REJECT`.
- `--trace` prints the state name and every tape before each step, and which `on` row fired.
- `--max-steps N` stops with a runtime error after N steps, to catch machines that never halt.

| Exit code | Meaning |
|---|---|
| 0 | halted or accepted |
| 1 | rejected |
| 2 | bad command line or unreadable file |
| 3 | compile error (syntax or semantic); nothing was run |
| 4 | runtime error, or `--max-steps` exceeded |
