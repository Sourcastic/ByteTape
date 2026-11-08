/*
 * ast.c -- node constructors and the state table
 *
 * Source line numbers come from flex's `yylineno` (enabled in lexer.l with `%option yylineno`). Because the parser sometimes reads one token ahead,
 * a line number can occasionally be off by one. That is fine for messages.
 */
#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int yylineno;      /* flex-generated lex.yy.c */

State states[MAX_STATES];
int   num_states  = 0;
int   start_state = -1;
static int start_is_explicit = 0;

static void *alloc_zeroed(size_t n)
{
    void *p = calloc(1, n);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

/* state table */

int state_intern(const char *name)
{
    for (int i = 0; i < num_states; i++)
        if (strcmp(states[i].name, name) == 0)
            return i;

    if (num_states >= MAX_STATES) {
        fprintf(stderr, "too many states (max %d)\n", MAX_STATES);
        exit(1);
    }
    states[num_states].name    = strdup(name);
    states[num_states].body    = NULL;
    states[num_states].defined = 0;
    states[num_states].line    = yylineno;   /* first use; overwritten by state_define */
    return num_states++;
}

int state_define(int id, Stmt *body)
{
    if (states[id].defined)
        return -1;
    states[id].defined = 1;
    states[id].body    = body;
    states[id].line    = yylineno;
    if (start_state < 0)
        start_state = id;                /* default: the first state in the file */
    return 0;
}

int state_mark_start(int id)
{
    if (start_is_explicit)
        return -1;
    start_is_explicit = 1;
    start_state = id;
    return 0;
}

int state_first_undefined(void)
{
    for (int i = 0; i < num_states; i++)
        if (!states[i].defined)
            return i;
    return -1;
}

/* operands */

Operand *operand_lit(int value)
{
    Operand *o = alloc_zeroed(sizeof *o);
    o->kind  = OPD_LIT;
    o->value = value;
    return o;
}

Operand *operand_cell(int tape, long offset)
{
    Operand *o = alloc_zeroed(sizeof *o);
    o->kind   = OPD_CELL;
    o->tape   = tape;
    o->offset = offset;
    return o;
}

Operand *operand_append(Operand *list, Operand *o)
{
    if (!list)
        return o;
    Operand *p = list;
    while (p->next)
        p = p->next;
    p->next = o;
    return list;
}

int operand_count(const Operand *list)
{
    int n = 0;
    for (; list; list = list->next)
        n++;
    return n;
}

/* conditions */

static Cond *new_cond(CondKind kind)
{
    Cond *c = alloc_zeroed(sizeof *c);
    c->kind = kind;
    c->line = yylineno;
    return c;
}

Cond *cond_cmp(CmpOp op, Operand *a, Operand *b)
{
    Cond *c = new_cond(COND_CMP);
    c->op = op;
    c->a  = a;
    c->b  = b;
    return c;
}

Cond *cond_and(Cond *l, Cond *r)
{
    Cond *c = new_cond(COND_AND);
    c->l = l;
    c->r = r;
    return c;
}

Cond *cond_or(Cond *l, Cond *r)
{
    Cond *c = new_cond(COND_OR);
    c->l = l;
    c->r = r;
    return c;
}

Cond *cond_not(Cond *x)
{
    Cond *c = new_cond(COND_NOT);
    c->l = x;
    return c;
}

/*  on-table patterns and rows */

Pattern *pattern_value(int value)
{
    Pattern *p = alloc_zeroed(sizeof *p);
    p->value = value;
    return p;
}

Pattern *pattern_any(void)
{
    Pattern *p = alloc_zeroed(sizeof *p);
    p->any = 1;
    return p;
}

Pattern *pattern_append(Pattern *list, Pattern *p)
{
    if (!list)
        return p;
    Pattern *q = list;
    while (q->next)
        q = q->next;
    q->next = p;
    return list;
}

int pattern_count(const Pattern *list)
{
    int n = 0;
    for (; list; list = list->next)
        n++;
    return n;
}

OnRow *onrow_new(Pattern *pats, Stmt *body)
{
    OnRow *r = alloc_zeroed(sizeof *r);
    r->pats = pats;
    r->body = body;
    r->line = yylineno;
    return r;
}

OnRow *onrow_append(OnRow *list, OnRow *row)
{
    if (!list)
        return row;
    OnRow *p = list;
    while (p->next)
        p = p->next;
    p->next = row;
    return list;
}

/* statements */

static Stmt *new_stmt(StmtKind kind)
{
    Stmt *s = alloc_zeroed(sizeof *s);
    s->kind = kind;
    s->line = yylineno;
    return s;
}

static Stmt *cell_stmt(StmtKind kind, int tape, long offset)
{
    Stmt *s   = new_stmt(kind);
    s->tape   = tape;
    s->offset = offset;
    return s;
}

Stmt *stmt_write(int tape, long offset, Operand *src)
{
    Stmt *s = cell_stmt(STMT_WRITE, tape, offset);
    s->src  = src;
    return s;
}

Stmt *stmt_move(int tape, int dir)
{
    Stmt *s = new_stmt(STMT_MOVE);
    s->tape = tape;
    s->dir  = dir;
    return s;
}

Stmt *stmt_inc  (int tape, long offset) { return cell_stmt(STMT_INC,   tape, offset); }
Stmt *stmt_dec  (int tape, long offset) { return cell_stmt(STMT_DEC,   tape, offset); }
Stmt *stmt_print (int tape, long offset) { return cell_stmt(STMT_PRINT,  tape, offset); }
Stmt *stmt_printn(int tape, long offset) { return cell_stmt(STMT_PRINTN, tape, offset); }
Stmt *stmt_input (int tape, long offset) { return cell_stmt(STMT_INPUT,  tape, offset); }

Stmt *stmt_if(Cond *cond, Stmt *then_body, Stmt *else_body)
{
    Stmt *s      = new_stmt(STMT_IF);
    s->cond      = cond;
    s->then_body = then_body;
    s->else_body = else_body;
    return s;
}

Stmt *stmt_on(Operand *cells, OnRow *rows)
{
    Stmt *s   = new_stmt(STMT_ON);
    s->cells  = cells;
    s->ncells = operand_count(cells);
    s->rows   = rows;
    return s;
}

Stmt *stmt_transition(int state)
{
    Stmt *s  = new_stmt(STMT_TRANSITION);
    s->state = state;
    return s;
}

Stmt *stmt_halt  (void) { return new_stmt(STMT_HALT);   }
Stmt *stmt_accept(void) { return new_stmt(STMT_ACCEPT); }
Stmt *stmt_reject(void) { return new_stmt(STMT_REJECT); }

Stmt *stmt_append(Stmt *list, Stmt *s)
{
    if (!list)
        return s;
    Stmt *p = list;
    while (p->next)
        p = p->next;
    p->next = s;
    return list;
}
