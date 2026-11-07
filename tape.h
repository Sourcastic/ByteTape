/*
 * tape.h -- the ByteTape runtime: a table of named, two-way infinite tapes.
 *
 * Every tape is identified by a small integer id (0, 1, 2, ...) handed out
 * when the tape is declared. The parser turns tape names into ids once, so
 * the rest of the program (AST, interpreter) never compares strings.
 *
 * Each cell is one uint8_t (0-255), so values wrap on overflow.
 * Cells that were never written read as 0.
 *
 * All offsets are RELATIVE TO THE HEAD:
 *     offset  0  ->  the cell under the head      ([Output])
 *     offset +1  ->  one cell to the right        ([Output + 1])
 *     offset -1  ->  one cell to the left         ([Output - 1])
 * Reading never moves the head.
 *
 * These are plain C functions with plain arguments on purpose: in Phase 2,
 * the generated LLVM IR will simply call them.
 */
#ifndef TAPE_H
#define TAPE_H

#include <stdint.h>
#include <stdio.h>

#define MAX_TAPES 64

/* ---- declaration (used by the parser) ---- */

/* Create a new empty tape. Returns its id, or -1 if the name already exists. */
int         tape_declare(const char *name);

/* Look up a tape by name. Returns its id, or -1 if it was never declared. */
int         tape_find(const char *name);

const char *tape_name(int id);
int         tape_count(void);

/* Fill cells 0..n-1 with vals[] and put the head on cell 0 (the first value). */
void        tape_set_initial(int id, const uint8_t *vals, int n);

/* ---- operations (used by the interpreter) ---- */

uint8_t     tape_read (int id, long offset);
void        tape_write(int id, long offset, uint8_t value);
void        tape_move (int id, int dir);      /* dir: -1 = left, +1 = right */

/* ---- debugging / output ---- */

/* Print every tape on its own line, names aligned, each line starting with `indent`:
 *     Output: 2 3 [4] 5
 * The cell in [brackets] is under the head. The range shown runs from cell 0 (or the
 * head, if further left) to the last non-zero cell (or the head, if further right). */
void        tape_dump(FILE *out, const char *indent);

#endif
