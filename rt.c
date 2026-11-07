/*
 * rt.c -- ByteTape runtime for COMPILED programs (Phase 2).
 *
 * The LLVM IR produced by codegen.c calls these functions. Together with
 * tape.c (the tape table from Phase 1) they are built into libbtrt.a, which
 * the driver script `bytetapec` links into every generated executable.
 *
 * Every function here uses plain `int` / `long` / `const char *` arguments so
 * the generated IR never has to worry about C ABI details for 8-bit values.
 * Values are truncated to 8 bits (0-255) when stored in a tape cell.
 *
 * Behaviour (output text, error messages, exit codes) mirrors interp.c, so a
 * compiled program and the Phase 1 interpreter can be compared directly.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tape.h"

/* Exit codes -- the same numbers as interp.h */
#define RT_EXIT_OK      0
#define RT_EXIT_REJECT  1
#define RT_EXIT_USAGE   2
#define RT_EXIT_RUNTIME 4

static int g_dump_tapes = 1;       /* print the final tapes when the program stops */

/* ---------- start-up ---------- */

/* Called first thing in the generated main(). Accepts: -q */
void rt_init(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0) {
            g_dump_tapes = 0;
        } else {
            fprintf(stderr, "usage: %s [-q]\n"
                            "  -q   quiet: do not print the final tapes when the program stops\n",
                    argv[0]);
            exit(RT_EXIT_USAGE);
        }
    }
}

/* ---------- tape access (thin wrappers around tape.c) ---------- */

int rt_read(int tape, long offset)
{
    return tape_read(tape, offset);
}

void rt_write(int tape, long offset, int value)
{
    tape_write(tape, offset, (uint8_t)value);
}

void rt_move(int tape, int dir)
{
    tape_move(tape, dir);
}

/* ---------- input / output ---------- */

void rt_print(int value)               /* print [Cell]  : the byte as a character */
{
    putchar((uint8_t)value);
}

void rt_printn(int value)              /* printn [Cell] : decimal number + newline */
{
    printf("%u\n", (unsigned)(uint8_t)value);
}

int rt_input(void)                     /* input [Cell]  : one byte, 0 at end of input */
{
    int c = getchar();
    return c == EOF ? 0 : c;
}

/* ---------- runtime errors ---------- */

static void runtime_error_begin(int line)
{
    fflush(stdout);
    fprintf(stderr, "runtime error (line %d): ", line);
}

/*
 * No row of an `on` matched. After the three fixed arguments come `n` pairs of
 * (const char *cell_description, int value), e.g. ("[Bits]", 1), ("[Rev + 1]", 97).
 */
void rt_no_match(int line, const char *state, int n, ...)
{
    va_list ap;
    runtime_error_begin(line);
    fprintf(stderr, "state '%s': no rule in 'on' matches ", state);
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        const char *desc = va_arg(ap, const char *);
        int         val  = va_arg(ap, int);
        fprintf(stderr, "%s%s=%d", i ? ", " : "", desc, val);
    }
    va_end(ap);
    fputc('\n', stderr);
    exit(RT_EXIT_RUNTIME);
}

/* A state body ran to its end without transition / halt / accept / reject. */
void rt_state_end(int line, const char *state)
{
    runtime_error_begin(line);
    fprintf(stderr, "state '%s' ended without transition, halt, accept or reject\n", state);
    exit(RT_EXIT_RUNTIME);
}

/* ---------- stopping ---------- */

/* verdict: 0 = halt, 1 = accept, 2 = reject. Returns main()'s exit code. */
int rt_finish(int verdict)
{
    fflush(stdout);
    if (g_dump_tapes)
        tape_dump(stdout, "");
    if (verdict == 1) { printf("ACCEPT\n"); return RT_EXIT_OK; }
    if (verdict == 2) { printf("REJECT\n"); return RT_EXIT_REJECT; }
    return RT_EXIT_OK;
}
