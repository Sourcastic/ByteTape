/*
 * codegen.h -- Phase 2: turns the parsed program into LLVM IR (textual .ll).
 *
 * The grammar (parser.y) calls codegen_emit() from the action of its top-level
 * `program` rule, once the whole program has been parsed and checked.
 */
#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdint.h>
#include <stdio.h>

/* Where the IR goes. NULL = compiler mode is off (Phase 1 interpreter). Set by main.c. */
extern FILE *codegen_out;

/* Remember the initial contents of a tape declaration `Name: {a, b, c}`.
 * Called by the grammar for every tape, right after the tape is created. */
void cg_tape_init(int tape_id, const uint8_t *vals, int n);

/* Write the complete LLVM IR module for the program in the AST / state table to `out`. */
void codegen_emit(FILE *out);

#endif
