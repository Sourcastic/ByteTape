/*
 * strict.c -- the `--strict` check: is this program a formal (multi-tape) Turing machine?
 *
 * The formal definition is a transition function
 *
 *      delta : Q x Gamma^k  ->  Q x Gamma^k x {L, S, R}^k
 *
 * The check runs on the finished AST, after the whole file has been parsed.
 * It changes nothing: a program that passes runs exactly as it would without --strict.
 */
#include "strict.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "tape.h"

int strict_mode = 0;

static int nerrors;

/* ---------- diagnostics ---------- */

static void err(int line, const char* fmt, ...)
{
    va_list ap;
    if (line > 0)
        fprintf(stderr, "line %d: error: strict mode: ", line);
    else
        fprintf(stderr, "error: strict mode: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    nerrors++;
}

static const char* kind_name(StmtKind k)
{
    switch (k) {
    case STMT_WRITE:
        return "write";
    case STMT_MOVE:
        return "move";
    case STMT_INC:
        return "inc";
    case STMT_DEC:
        return "dec";
    case STMT_IF:
        return "if";
    case STMT_ON:
        return "on";
    case STMT_TRANSITION:
        return "transition";
    case STMT_HALT:
        return "halt";
    case STMT_ACCEPT:
        return "accept";
    case STMT_REJECT:
        return "reject";
    case STMT_PRINT:
        return "print";
    case STMT_PRINTN:
        return "printn";
    case STMT_INPUT:
        return "input";
    }
    return "?";
}

/* A symbol as the user would write it: _  'a'  or a number. */
static void sym_str(int v, char* buf, size_t n)
{
    if (v == 0)
        snprintf(buf, n, "_");
    else if (v >= 32 && v < 127)
        snprintf(buf, n, "'%c'", v);
    else
        snprintf(buf, n, "%d", v);
}

/* ---------- sets of symbols (bit sets over 0..255) ---------- */

typedef struct {
    uint64_t w[4];
} Set;

static Set gamma_set; /* the tape alphabet Gamma */

static int set_has(const Set* s, int v) { return (s->w[v >> 6] >> (v & 63)) & 1; }
static void set_add(Set* s, int v) { s->w[v >> 6] |= (uint64_t)1 << (v & 63); }
static int set_empty(const Set* s) { return !(s->w[0] | s->w[1] | s->w[2] | s->w[3]); }

static Set set_and(const Set* a, const Set* b)
{
    Set r;
    for (int i = 0; i < 4; i++)
        r.w[i] = a->w[i] & b->w[i];
    return r;
}

static Set set_minus(const Set* a, const Set* b)
{
    Set r;
    for (int i = 0; i < 4; i++)
        r.w[i] = a->w[i] & ~b->w[i];
    return r;
}

static int set_first(const Set* s)
{
    for (int v = 0; v < 256; v++)
        if (set_has(s, v))
            return v;
    return -1;
}

/* ---------- 1. whole-program checks ---------- */

static int check_program(void)
{
    if (!tape_alphabet_active()) {
        err(0, "an '%%%% alphabet' section is required (declare the input and tape symbols)");
        return 0;
    }
    for (int v = 0; v < 256; v++)
        if (tape_in_tape_alphabet(v))
            set_add(&gamma_set, v);

    int k = tape_count();
    if (k < 1) {
        err(0, "the machine needs at least one tape");
        return 0;
    }
    for (int t = 1; t < k; t++)
        if (!tape_is_blank(t))
            err(0, "tape '%s' must start blank: only the first tape ('%s') holds the input",
                tape_name(t), tape_name(0));
    return 1;
}

/* ---------- 2. one row:  [writes] [moves] terminal ---------- */

static void check_row(const OnRow* row)
{
    int wrote[MAX_TAPES] = { 0 };
    int moved[MAX_TAPES] = { 0 };
    int in_moves = 0;
    int terminal = 0;

    for (const Stmt* s = row->body; s; s = s->next) {
        if (terminal) {
            err(s->line, "nothing may follow transition / accept / reject / halt in a row");
            return;
        }
        switch (s->kind) {
        case STMT_WRITE:
            if (s->offset != 0)
                err(s->line, "'write' must target the cell under the head, not [%s %c %ld]",
                    tape_name(s->tape), s->offset < 0 ? '-' : '+', labs(s->offset));
            if (!s->src || s->src->kind != OPD_LIT)
                err(s->line, "'write' must store a symbol literal, not copy another cell");
            if (in_moves)
                err(s->line, "writes must come before moves "
                             "(a write always goes to the cell the head was on)");
            if (wrote[s->tape]++)
                err(s->line, "tape '%s' is written twice in one row", tape_name(s->tape));
            break;
        case STMT_MOVE:
            in_moves = 1;
            if (moved[s->tape]++)
                err(s->line, "tape '%s' is moved twice in one row", tape_name(s->tape));
            break;
        case STMT_TRANSITION:
        case STMT_ACCEPT:
        case STMT_REJECT:
        case STMT_HALT:
            terminal = 1;
            break;
        default:
            err(s->line, "'%s' is not allowed: a row may only write, move, and then "
                         "transition / accept / reject / halt",
                kind_name(s->kind));
            break;
        }
    }
    if (!terminal)
        err(row->line, "a row must end with transition, accept, reject or halt");
}

/* ---------- 3. coverage of the table over Gamma^k ---------- */

#define MAX_BOXES 20000

typedef struct {
    Set col[MAX_ON_CELLS];
} Box; /* a product of symbol sets */

static int box_overlaps(const Box* b, const Box* r, int k)
{
    for (int c = 0; c < k; c++) {
        Set i = set_and(&b->col[c], &r->col[c]);
        if (set_empty(&i))
            return 0;
    }
    return 1;
}

/* Walk the rows in order, keeping the part of Gamma^k that no earlier row matched. */
static void check_coverage(const State* st, const Stmt* on)
{
    int k = on->ncells;
    Box* open = malloc(sizeof(Box) * MAX_BOXES);
    Box* next = malloc(sizeof(Box) * MAX_BOXES);
    if (!open || !next) {
        free(open);
        free(next);
        err(on->line, "out of memory");
        return;
    }

    int nopen = 1;
    for (int c = 0; c < k; c++)
        open[0].col[c] = gamma_set;

    int rown = 0;
    for (const OnRow* r = on->rows; r; r = r->next) {
        rown++;
        if (pattern_count(r->pats) != k) /* already reported by the parser */
            continue;

        Box rb;
        int c = 0;
        for (const Pattern* p = r->pats; p; p = p->next, c++) {
            memset(&rb.col[c], 0, sizeof(Set));
            if (p->any)
                rb.col[c] = gamma_set;
            else
                set_add(&rb.col[c], p->value);
        }

        int hit = 0;
        for (int i = 0; i < nopen; i++)
            if (box_overlaps(&open[i], &rb, k)) {
                hit = 1;
                break;
            }
        if (!hit)
            err(r->line, "state '%s': row %d can never match, earlier rows already cover it",
                st->name, rown);

        /* open := open minus rb, as disjoint boxes */
        int nn = 0;
        for (int i = 0; i < nopen; i++) {
            const Box* b = &open[i];
            if (!box_overlaps(b, &rb, k)) {
                if (nn < MAX_BOXES)
                    next[nn++] = *b;
                continue;
            }
            Box piece = *b; /* columns left of c already narrowed */
            for (int col = 0; col < k; col++) {
                Set rest = set_minus(&b->col[col], &rb.col[col]);
                if (!set_empty(&rest) && nn < MAX_BOXES) {
                    next[nn] = piece;
                    next[nn].col[col] = rest;
                    nn++;
                }
                piece.col[col] = set_and(&b->col[col], &rb.col[col]);
            }
        }
        if (nn >= MAX_BOXES) {
            err(on->line, "state '%s': the table is too large to check for completeness", st->name);
            free(open);
            free(next);
            return;
        }
        Box* tmp = open;
        open = next;
        next = tmp;
        nopen = nn;
    }

    if (nopen > 0) {
        char msg[512];
        int len = snprintf(msg, sizeof msg, "state '%s' does not cover every symbol combination; missing e.g.",
            st->name);
        for (int i = 0; i < nopen && i < 3 && len < (int)sizeof msg - 80; i++) {
            len += snprintf(msg + len, sizeof msg - len, "%s (", i ? "," : "");
            for (int c = 0; c < k; c++) {
                char sb[16];
                sym_str(set_first(&open[i].col[c]), sb, sizeof sb);
                len += snprintf(msg + len, sizeof msg - len, "%s%s", c ? ", " : "", sb);
            }
            len += snprintf(msg + len, sizeof msg - len, ")");
        }
        if (nopen > 3)
            snprintf(msg + len, sizeof msg - len, " ...");
        err(on->line, "%s -- add rows or a final '*' row", msg);
    }
    free(open);
    free(next);
}

/* ---------- 4. one state ---------- */

static void check_state(const State* st)
{
    const Stmt* b = st->body;

    if (!b) {
        err(st->line, "state '%s' is empty: a strict state is a single 'on' table", st->name);
        return;
    }
    if (b->kind != STMT_ON) {
        err(b->line, "state '%s' must be a single 'on' table, but it starts with '%s'",
            st->name, kind_name(b->kind));
        return;
    }
    if (b->next)
        err(b->next->line, "state '%s': nothing may follow the 'on' table", st->name);

    /* the `on` must read every tape, once, at offset 0 */
    int k = tape_count();
    int seen[MAX_TAPES] = { 0 };
    int ok = 1;
    for (const Operand* o = b->cells; o; o = o->next) {
        if (o->offset != 0) {
            err(b->line, "state '%s': 'on' must read the cell under the head, not [%s %c %ld]",
                st->name, tape_name(o->tape), o->offset < 0 ? '-' : '+', labs(o->offset));
            ok = 0;
        }
        if (seen[o->tape]++) {
            err(b->line, "state '%s': tape '%s' is listed twice in 'on'", st->name, tape_name(o->tape));
            ok = 0;
        }
    }
    for (int t = 0; t < k; t++)
        if (!seen[t]) {
            err(b->line, "state '%s': 'on' must list every tape, but '%s' is missing",
                st->name, tape_name(t));
            ok = 0;
        }

    for (const OnRow* r = b->rows; r; r = r->next)
        check_row(r);

    if (ok)
        check_coverage(st, b);
}

/* ---------- entry point ---------- */

int strict_check(void)
{
    nerrors = 0;
    if (!check_program())
        return nerrors;
    for (int i = 0; i < num_states; i++)
        if (states[i].defined)
            check_state(&states[i]);
    return nerrors;
}
