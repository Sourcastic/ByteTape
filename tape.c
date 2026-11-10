/*
 * tape.c -- implementation of the tape table
 *
 * Representation of one tape:
 *
 *      cells[]   a heap array, grown on demand in either direction
 *      origin    array index that corresponds to logical position 0
 *      head      logical head position (may be negative)
 *
 * logical position p  <->  array index (origin + p)
 *
 * Growing to the left shifts the data right inside a new array and increases
 * `origin` by the same amount, so logical positions never change.
 * Reading outside the allocated array returns 0 without allocating.
 * Only writing grows the array.
 */
#include "tape.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_CAP 16

typedef struct {
    char    *name;
    uint8_t *cells;
    long     cap;      /* number of allocated cells */
    long     origin;   /* index in cells[] of logical position 0 */
    long     head;     /* logical position of the head */
} Tape;

static Tape tapes[MAX_TAPES];
static int  ntapes = 0;

/* helpers */

static void die(const char *msg)
{
    fprintf(stderr, "runtime error: %s\n", msg);
    exit(1);
}

static Tape *get(int id)
{
    if (id < 0 || id >= ntapes)
        die("invalid tape id");
    return &tapes[id];
}

/* Make sure logical position `pos` is inside the allocated array. */
static void ensure(Tape *t, long pos)
{
    long idx = t->origin + pos;
    if (idx >= 0 && idx < t->cap)
        return;

    long grow_left = 0;
    long new_cap   = t->cap;

    if (idx < 0) {                       /* need room on the left */
        grow_left = -idx + t->cap / 2 + 8;
        new_cap   = t->cap + grow_left;
    } else {                             /* need room on the right */
        new_cap = idx + 1 + t->cap / 2 + 8;
    }

    uint8_t *fresh = calloc((size_t)new_cap, 1);   /* zero-filled = blank */
    if (!fresh)
        die("out of memory while growing a tape");
    memcpy(fresh + grow_left, t->cells, (size_t)t->cap);
    free(t->cells);

    t->cells   = fresh;
    t->cap     = new_cap;
    t->origin += grow_left;
}

static void put(Tape *t, long pos, uint8_t value)
{
    ensure(t, pos);
    t->cells[t->origin + pos] = value;
}

/*  declaration */

int tape_find(const char *name)
{
    for (int i = 0; i < ntapes; i++)
        if (strcmp(tapes[i].name, name) == 0)
            return i;
    return -1;
}

int tape_declare(const char *name)
{
    if (tape_find(name) >= 0)
        return -1;
    if (ntapes >= MAX_TAPES)
        die("too many tapes");

    Tape *t  = &tapes[ntapes];
    t->name   = strdup(name);
    t->cap    = INITIAL_CAP;
    t->cells  = calloc((size_t)t->cap, 1);
    t->origin = 0;
    t->head   = 0;
    if (!t->name || !t->cells)
        die("out of memory");
    return ntapes++;
}

const char *tape_name(int id) { return get(id)->name; }
int         tape_count(void)  { return ntapes; }

int tape_is_blank(int id)
{
    Tape *t = get(id);
    for (long k = 0; k < t->cap; k++)
        if (t->cells[k])
            return 0;
    return 1;
}

void tape_set_initial(int id, const uint8_t *vals, int n)
{
    Tape *t = get(id);
    for (int i = 0; i < n; i++)
        put(t, i, vals[i]);
    t->head = 0;
}

/*  operations */

uint8_t tape_read(int id, long offset)
{
    Tape *t  = get(id);
    long idx = t->origin + t->head + offset;
    if (idx < 0 || idx >= t->cap)
        return 0;                        /* never written = blank = 0 */
    return t->cells[idx];
}

void tape_write(int id, long offset, uint8_t value)
{
    Tape *t = get(id);
    put(t, t->head + offset, value);
}

void tape_move(int id, int dir)
{
    Tape *t = get(id);
    if (dir != -1 && dir != 1)
        die("tape_move: direction must be -1 or +1");
    t->head += dir;
}

/* output */

void tape_dump(FILE *out, const char *indent)
{
    int width = 0;                      /* longest tape name, for alignment */
    for (int i = 0; i < ntapes; i++) {
        int len = (int)strlen(tapes[i].name);
        if (len > width) width = len;
    }

    for (int i = 0; i < ntapes; i++) {
        Tape *t = &tapes[i];

        /* range = from the leftmost to the rightmost non-zero cell,
         * widened so that cell 0 (where declared data starts) and the head
         * are always included */
        long lo = t->head < 0 ? t->head : 0;
        long hi = t->head > 0 ? t->head : 0;
        for (long k = 0; k < t->cap; k++) {
            if (t->cells[k]) {
                long p = k - t->origin;
                if (p < lo) lo = p;
                if (p > hi) hi = p;
            }
        }

        fprintf(out, "%s%s:%*s", indent, t->name, width - (int)strlen(t->name), "");
        for (long p = lo; p <= hi; p++) {
            long idx = t->origin + p;
            unsigned v = (idx >= 0 && idx < t->cap) ? t->cells[idx] : 0;
            if (p == t->head) fprintf(out, " [%u]", v);
            else              fprintf(out, " %u", v);
        }
        fprintf(out, "\n");
    }
}

/*alphabet */

#define SYM_GAMMA 1
#define SYM_SIGMA 2

static uint8_t sym_flags[256];
static int alphabet_on = 0;

void tape_alphabet_enable(void)
{
    alphabet_on = 1;
    sym_flags[0] |= SYM_GAMMA;
}

int tape_alphabet_active(void)
{
    return alphabet_on;
}

void tape_alphabet_add(int sym, int is_input)
{
    if (sym < 0 || sym > 255)
        return;

    sym_flags[sym] |= SYM_GAMMA;

    if (is_input)
        sym_flags[sym] |= SYM_SIGMA;
}

int tape_in_input_alphabet(int sym)
{
    return !alphabet_on ||
           (sym >= 0 && sym <= 255 && (sym_flags[sym] & SYM_SIGMA));
}

int tape_in_tape_alphabet(int sym)
{
    return !alphabet_on ||
           (sym >= 0 && sym <= 255 && (sym_flags[sym] & SYM_GAMMA));
}

void tape_symbol_name(int sym, char *buf, size_t n)
{
    if (sym == 0)
        snprintf(buf, n, "_ (blank)");
    else if (sym >= 32 && sym < 127)
        snprintf(buf, n, "'%c' (%d)", sym, sym);
    else
        snprintf(buf, n, "%d", sym);
}