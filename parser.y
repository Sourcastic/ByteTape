/*
 * parser.y -- the ByteTape grammar. Builds the AST (see ast.h); it does not
 * run anything. interp.c runs the tree afterwards.
 *
 * Program shape:
 *
 *      # comment
 *      %% tapes
 *      Name: {1, 2, 'x', _}               tape declarations
 *
 *      %% states
 *      state Name { statements }          state definitions
 *      start state Name { statements }    (optional: pick the start state)
 */
%code requires {
#include "ast.h"
}

%{
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "ast.h"
#include "tape.h"

int  yylex(void);
void yyerror(const char *msg);
extern int yylineno;

int parse_errors = 0;                 /* read by main.c */

/* ---------- diagnostics ---------- */

static void vdiag(int line, const char *kind, const char *fmt, va_list ap)
{
    fprintf(stderr, "line %d: %s: ", line, kind);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

/* A non-syntax error (undeclared tape, duplicate state, ...). Parsing continues. */
static void semantic_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vdiag(yylineno, "error", fmt, ap);
    va_end(ap);
    parse_errors++;
}

static void error_at(int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vdiag(line, "error", fmt, ap);
    va_end(ap);
    parse_errors++;
}

static void warning_at(int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vdiag(line, "warning", fmt, ap);
    va_end(ap);
}

/* ---------- helpers used by the grammar actions ---------- */

/* Turn a tape name into its id; report an error if it was never declared. */
static int lookup_tape(char *name)
{
    int id = tape_find(name);
    if (id < 0)
        semantic_error("tape '%s' is not declared", name);
    free(name);
    return id;
}

/* Every literal must fit in a cell. */
static int check_lit(int v)
{
    if (v < 0 || v > 255) {
        semantic_error("value %d is outside 0-255", v);
        return 0;
    }
    return v;
}

/* Statements that take just a cell target (inc, dec, print, printn, input). */
static Stmt *cell_stmt(Stmt *(*make)(int, long), Operand *cell)
{
    Stmt *s = make(cell->tape, cell->offset);
    free(cell);
    return s;
}

/* Collects the values of one tape declaration {a, b, c} */
static uint8_t *init_buf = NULL;
static int      init_n = 0, init_cap = 0;

static void init_push(int v)
{
    if (init_n == init_cap) {
        init_cap = init_cap ? init_cap * 2 : 16;
        init_buf = realloc(init_buf, (size_t)init_cap);
        if (!init_buf) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    init_buf[init_n++] = (uint8_t)check_lit(v);
}

static void define_state(char *name, Stmt *body, int is_start)
{
    int id = state_intern(name);
    if (state_define(id, body) < 0)
        semantic_error("state '%s' is defined twice", name);
    else if (is_start && state_mark_start(id) < 0)
        semantic_error("more than one 'start state'");
    free(name);
}

/* Does row pattern `a` match everything that row pattern `b` matches? */
static int covers(const Pattern *a, const Pattern *b)
{
    for (; a && b; a = a->next, b = b->next)
        if (!a->any && (b->any || a->value != b->value))
            return 0;
    return 1;
}

/* Build an `on` statement and check its table. */
static Stmt *make_on(Operand *cells, OnRow *rows)
{
    int ncells = operand_count(cells);
    if (ncells > MAX_ON_CELLS)
        semantic_error("'on' lists %d cells (the most allowed is %d)", ncells, MAX_ON_CELLS);

    int n = 0;
    for (OnRow *r = rows; r; r = r->next) {
        n++;
        int np = pattern_count(r->pats);
        if (np != ncells) {
            error_at(r->line, "row %d has %d pattern(s) but 'on' lists %d cell(s)", n, np, ncells);
            continue;
        }
        int m = 0;
        for (OnRow *e = rows; e != r; e = e->next) {
            m++;
            if (pattern_count(e->pats) == ncells && covers(e->pats, r->pats)) {
                warning_at(r->line, "row %d can never match: row %d already covers it", n, m);
                break;
            }
        }
    }
    return stmt_on(cells, rows);
}
%}

%define parse.error verbose

%union {
    int      num;
    char    *str;
    Operand *opd;
    Cond    *cond;
    Stmt    *stmt;
    OnRow   *row;
    Pattern *pat;
}

%token <num> NUMBER
%token <str> IDENT
%token SEC_TAPES   "%% tapes"
%token SEC_STATES  "%% states"
%token BAD_SECTION "bad section marker"
%token STATE START TRANSITION HALT KW_ACCEPT KW_REJECT WRITE MOVE INC DEC
%token IF ELSE ON AND OR NOT PRINT PRINTN INPUT LEFT RIGHT ARROW
%token EQ NE LE GE

%type <opd>  operand cell_ref cell_list
%type <cond> cond
%type <stmt> stmt if_stmt on_stmt stmt_list block
%type <row>  row row_list
%type <pat>  pattern pattern_list
%type <num>  dir relop

/* lowest to highest precedence */
%left  OR
%left  AND
%right NOT

%%

program
    : SEC_TAPES tape_decls SEC_STATES state_defs
    ;

/* ---------- tapes ---------- */

tape_decls
    : /* empty */
    | tape_decls tape_decl
    ;

tape_decl
    : IDENT ':' '{' init_list_opt '}'
        {
            int id = tape_declare($1);
            if (id < 0)
                semantic_error("tape '%s' is declared twice", $1);
            else
                tape_set_initial(id, init_buf, init_n);
            init_n = 0;
            free($1);
        }
    ;

init_list_opt
    : /* empty */
    | init_list
    ;

init_list
    : NUMBER                    { init_push($1); }
    | init_list ',' NUMBER      { init_push($3); }
    ;

/* ---------- states ---------- */

state_defs
    : /* empty */
    | state_defs state_def
    ;

state_def
    : STATE IDENT block             { define_state($2, $3, 0); }
    | START STATE IDENT block       { define_state($3, $4, 1); }
    ;

stmt_list
    : /* empty */               { $$ = NULL; }
    | stmt_list stmt            { $$ = stmt_append($1, $2); }
    ;

block
    : '{' stmt_list '}'         { $$ = $2; }
    ;

/* ---------- statements ---------- */

stmt
    : WRITE cell_ref operand    { $$ = stmt_write($2->tape, $2->offset, $3); free($2); }
    | MOVE IDENT dir            { $$ = stmt_move(lookup_tape($2), $3); }
    | INC cell_ref              { $$ = cell_stmt(stmt_inc,    $2); }
    | DEC cell_ref              { $$ = cell_stmt(stmt_dec,    $2); }
    | PRINT cell_ref            { $$ = cell_stmt(stmt_print,  $2); }
    | PRINTN cell_ref           { $$ = cell_stmt(stmt_printn, $2); }
    | INPUT cell_ref            { $$ = cell_stmt(stmt_input,  $2); }
    | TRANSITION IDENT          { $$ = stmt_transition(state_intern($2)); free($2); }
    | HALT                      { $$ = stmt_halt(); }
    | KW_ACCEPT                 { $$ = stmt_accept(); }
    | KW_REJECT                 { $$ = stmt_reject(); }
    | if_stmt                   { $$ = $1; }
    | on_stmt                   { $$ = $1; }
    ;

dir
    : LEFT                      { $$ = -1; }
    | RIGHT                     { $$ =  1; }
    ;

if_stmt
    : IF cond block                     { $$ = stmt_if($2, $3, NULL); }
    | IF cond block ELSE block          { $$ = stmt_if($2, $3, $5); }
    | IF cond block ELSE if_stmt        { $$ = stmt_if($2, $3, $5); }   /* else if */
    ;

/* ---------- on: a rule table ---------- */

on_stmt
    : ON cell_list '{' row_list '}'     { $$ = make_on($2, $4); }
    ;

cell_list
    : cell_ref                          { $$ = $1; }
    | cell_list ',' cell_ref            { $$ = operand_append($1, $3); }
    ;

row_list
    : row                               { $$ = $1; }
    | row_list row                      { $$ = onrow_append($1, $2); }
    ;

row
    : pattern_list ARROW block          { $$ = onrow_new($1, $3); }
    ;

pattern_list
    : pattern                           { $$ = $1; }
    | pattern_list ',' pattern          { $$ = pattern_append($1, $3); }
    ;

pattern
    : NUMBER                            { $$ = pattern_value(check_lit($1)); }
    | '*'                               { $$ = pattern_any(); }
    ;

/* ---------- operands and conditions ---------- */

/* [Tape]  [Tape + n]  [Tape - n]  -- a cell relative to that tape's head */
cell_ref
    : '[' IDENT ']'                 { $$ = operand_cell(lookup_tape($2), 0); }
    | '[' IDENT '+' NUMBER ']'      { $$ = operand_cell(lookup_tape($2),  $4); }
    | '[' IDENT '-' NUMBER ']'      { $$ = operand_cell(lookup_tape($2), -$4); }
    ;

operand
    : NUMBER                    { $$ = operand_lit(check_lit($1)); }
    | cell_ref                  { $$ = $1; }
    ;

cond
    : operand relop operand     { $$ = cond_cmp((CmpOp)$2, $1, $3); }
    | cond AND cond             { $$ = cond_and($1, $3); }
    | cond OR cond              { $$ = cond_or($1, $3); }
    | NOT cond                  { $$ = cond_not($2); }
    | '(' cond ')'              { $$ = $2; }
    ;

relop
    : EQ                        { $$ = CMP_EQ; }
    | NE                        { $$ = CMP_NE; }
    | '<'                       { $$ = CMP_LT; }
    | '>'                       { $$ = CMP_GT; }
    | LE                        { $$ = CMP_LE; }
    | GE                        { $$ = CMP_GE; }
    ;

%%

void yyerror(const char *msg)
{
    fprintf(stderr, "line %d: %s\n", yylineno, msg);
    parse_errors++;
}
