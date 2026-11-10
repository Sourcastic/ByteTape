/*
 * strict.h -- `--strict`: accept only programs that are formal Turing machines.
 *
 * A strict program is a transition table  delta : Q x Gamma^k -> Q x Gamma^k x {L,S,R}^k
 * (k tapes, deterministic). strict_check() walks the finished AST and reports every
 * place where a program steps outside that definition. It is compile-time only: nothing
 * in the interpreter or the runtime library changes.
 */
#ifndef STRICT_H
#define STRICT_H

extern int strict_mode;          /* set by main.c when --strict is given */

/* Check the parsed program. Prints one diagnostic per problem; returns how many. */
int strict_check(void);

#endif
