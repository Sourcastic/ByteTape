/*
 * interp.c -- executes the AST.
 *
 *   operand_value() / eval_cond()   read-only: look at cells and literals
 *   exec_block()                    runs a list of statements and reports how it ended:
 *                                     FLOW_NEXT        ran off the end of the list
 *                                     FLOW_TRANSITION  hit `transition X` (target in *next_state)
 *                                     FLOW_HALT / FLOW_ACCEPT / FLOW_REJECT
 *   run_program()                   the main loop: run a state, follow its transition, repeat
 */
#include "interp.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ast.h"
#include "tape.h"

typedef enum { FLOW_NEXT, FLOW_TRANSITION, FLOW_HALT, FLOW_ACCEPT, FLOW_REJECT } Flow;

static int g_trace   = 0;     /* --trace */
static int g_current = -1;    /* id of the state being run, for messages */

static void runtime_error(int line, const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "runtime error (line %d): ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_RUNTIME);
}

/* ---------- operands and conditions ---------- */

static int operand_value(const Operand *o)
{
    return o->kind == OPD_LIT ? o->value : tape_read(o->tape, o->offset);
}

static int eval_cond(const Cond *c)
{
    switch (c->kind) {
    case COND_CMP: {
        int a = operand_value(c->a);
        int b = operand_value(c->b);
        switch (c->op) {
        case CMP_EQ: return a == b;
        case CMP_NE: return a != b;
        case CMP_LT: return a <  b;
        case CMP_GT: return a >  b;
        case CMP_LE: return a <= b;
        case CMP_GE: return a >= b;
        }
        break;
    }
    case COND_AND: return eval_cond(c->l) && eval_cond(c->r);
    case COND_OR:  return eval_cond(c->l) || eval_cond(c->r);
    case COND_NOT: return !eval_cond(c->l);
    }
    runtime_error(c->line, "internal error: bad condition node");
    return 0;
}

/* ---------- `on` support ---------- */

/* Writes e.g.  "[Bits]=1, [Rev + 1]=97"  describing the cells an `on` looked at. */
static void describe_cells(char *buf, size_t n, const Operand *cells, const int *vals)
{
    size_t used = 0;
    int i = 0;
    buf[0] = '\0';
    for (const Operand *c = cells; c && used < n; c = c->next, i++) {
        const char *name = tape_name(c->tape);
        int w;
        if (c->offset == 0)
            w = snprintf(buf + used, n - used, "%s[%s]=%d", i ? ", " : "", name, vals[i]);
        else
            w = snprintf(buf + used, n - used, "%s[%s %c %ld]=%d", i ? ", " : "", name,
                         c->offset < 0 ? '-' : '+', c->offset < 0 ? -c->offset : c->offset, vals[i]);
        if (w < 0) break;
        used += (size_t)w;
    }
}

/* ---------- statements ---------- */

static Flow exec_block(const Stmt *s, int *next_state)
{
    for (; s; s = s->next) {
        switch (s->kind) {
        case STMT_WRITE:
            tape_write(s->tape, s->offset, (uint8_t)operand_value(s->src));
            break;

        case STMT_MOVE:
            tape_move(s->tape, s->dir);
            break;

        case STMT_INC:   /* int -> uint8_t conversion wraps modulo 256 */
            tape_write(s->tape, s->offset, (uint8_t)(tape_read(s->tape, s->offset) + 1));
            break;

        case STMT_DEC:
            tape_write(s->tape, s->offset, (uint8_t)(tape_read(s->tape, s->offset) - 1));
            break;

        case STMT_IF: {
            const Stmt *branch = eval_cond(s->cond) ? s->then_body : s->else_body;
            Flow f = exec_block(branch, next_state);
            if (f != FLOW_NEXT)
                return f;            /* a transition/halt inside the branch ends the state */
            break;
        }

        case STMT_ON: {
            /* 1. read every cell once, BEFORE any row runs */
            int vals[MAX_ON_CELLS];
            int i = 0;
            for (const Operand *c = s->cells; c; c = c->next, i++)
                vals[i] = tape_read(c->tape, c->offset);

            /* 2. first matching row wins */
            int rownum = 0;
            const OnRow *hit = NULL;
            for (const OnRow *r = s->rows; r && !hit; r = r->next) {
                rownum++;
                int ok = 1;
                i = 0;
                for (const Pattern *p = r->pats; p; p = p->next, i++)
                    if (!p->any && p->value != vals[i]) { ok = 0; break; }
                if (ok) hit = r;
            }

            char desc[256];
            describe_cells(desc, sizeof desc, s->cells, vals);

            if (!hit)
                runtime_error(s->line, "state '%s': no rule in 'on' matches %s",
                              states[g_current].name, desc);
            if (g_trace)
                printf("    on %s -> row %d\n", desc, rownum);

            /* 3. run it; if it does not leave the state, carry on after the `on` */
            Flow f = exec_block(hit->body, next_state);
            if (f != FLOW_NEXT)
                return f;
            break;
        }

        case STMT_TRANSITION:
            *next_state = s->state;
            return FLOW_TRANSITION;

        case STMT_HALT:   return FLOW_HALT;
        case STMT_ACCEPT: return FLOW_ACCEPT;
        case STMT_REJECT: return FLOW_REJECT;

        case STMT_PRINT:
            putchar((uint8_t)tape_read(s->tape, s->offset));
            break;

        case STMT_PRINTN:
            printf("%u\n", (unsigned)tape_read(s->tape, s->offset));
            break;

        case STMT_INPUT: {
            int c = getchar();
            tape_write(s->tape, s->offset, (uint8_t)(c == EOF ? 0 : c));
            break;
        }
        }
    }
    return FLOW_NEXT;
}

/* ---------- main loop ---------- */

int run_program(const RunOptions *opts)
{
    int  current = start_state;
    int  next    = -1;
    long steps   = 0;
    Flow f;

    g_trace = opts->trace;

    for (;;) {
        g_current = current;
        steps++;
        if (opts->max_steps > 0 && steps > opts->max_steps)
            runtime_error(states[current].line,
                          "exceeded --max-steps %ld (the machine may never stop)", opts->max_steps);

        if (g_trace) {
            printf("step %ld: state %s\n", steps, states[current].name);
            tape_dump(stdout, "    ");
        }

        f = exec_block(states[current].body, &next);

        if (f == FLOW_TRANSITION) {
            current = next;
            continue;
        }
        if (f == FLOW_NEXT)
            runtime_error(states[current].line,
                          "state '%s' ended without transition, halt, accept or reject",
                          states[current].name);
        break;                       /* HALT, ACCEPT or REJECT */
    }

    if (g_trace)
        printf("stopped after %ld step(s)\n", steps);
    fflush(stdout);
    if (opts->dump_tapes)
        tape_dump(stdout, "");

    if (f == FLOW_ACCEPT) { printf("ACCEPT\n"); return EXIT_OK; }
    if (f == FLOW_REJECT) { printf("REJECT\n"); return EXIT_REJECT; }
    return EXIT_OK;
}
