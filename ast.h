/*
 * ast.h -- the Abstract Syntax Tree. THIS IS THE CONTRACT between:
 *
 *      parser.y  BUILDS these nodes using the constructors below
 *      interp.c  WALKS these nodes and executes them
 *      codegen.c WALK the same nodes and emit LLVM IR
 *
 * Tape names and state names are converted to integer ids at parse time.
 *
 * ByteTape has no expression language. The only things a statement can look at
 * are CELLS and LITERALS (an "operand"), and the only decisions are
 * conditions (two operands compared) and `on` rule tables.
 */
#ifndef AST_H
#define AST_H

#define MAX_ON_CELLS 16     /* most cells one `on` statement may inspect */

/*   OPERANDS : An operand is either a literal 0-255 or a tape cell.
 */
typedef enum { OPD_LIT, OPD_CELL } OperandKind;

typedef struct Operand {
    OperandKind     kind;
    int             value;        /* OPD_LIT:  0-255 ('_' is 0)           */
    int             tape;         /* OPD_CELL: tape id                    */
    long            offset;       /* OPD_CELL: offset from the head       */
    struct Operand *next;
} Operand;

/* CONDITIONS : Used by `if`. Evaluating a condition never changes a tape.
 */
typedef enum { CMP_EQ, CMP_NE, CMP_LT, CMP_GT, CMP_LE, CMP_GE } CmpOp;
typedef enum { COND_CMP, COND_AND, COND_OR, COND_NOT } CondKind;

typedef struct Cond {
    CondKind     kind;
    CmpOp        op;              /* COND_CMP                               */
    Operand     *a, *b;           /* COND_CMP: a op b                       */
    struct Cond *l, *r;           /* COND_AND / COND_OR: both; COND_NOT: l  */
    int          line;
} Cond;

/*  ON RULE TABLES 
 *   on [A], [B] {  pat, pat -> { ... }   pat, pat -> { ... } }
 * One Pattern per scrutinee cell: a value, or "any" (written *).
 */
typedef struct Pattern {
    int             any;          /* 1 = matches every value (*)  */
    int             value;        /* otherwise: the value to match */
    struct Pattern *next;
} Pattern;

struct Stmt;                      /* forward declaration */

typedef struct OnRow {
    Pattern         *pats;
    struct Stmt     *body;
    struct OnRow    *next;
    int              line;
} OnRow;

/*  STATEMENTS : A block is a singly linked list of statements (via `next`).
 */
typedef enum {
    STMT_WRITE,        
    STMT_MOVE,         
    STMT_INC,          /* inc [Tape + n]   (255 wraps to 0) */
    STMT_DEC,          /* dec [Tape + n]   (0 wraps to 255) */
    STMT_IF,           
    STMT_ON,           /* on [A], [B] { pats -> { ... } ... } */
    STMT_TRANSITION,   /* transition StateName            */
    STMT_HALT,         
    STMT_ACCEPT,       
    STMT_REJECT,       
    STMT_PRINT,       
    STMT_PRINTN,       /* printn[Cell]  -> the value as a number + newline */
    STMT_INPUT         
} StmtKind;

typedef struct Stmt {
    StmtKind     kind;
    struct Stmt *next;            /* next statement in the same block (or NULL) */

    int          tape;            /* WRITE INC DEC PRINT PRINTN INPUT: target tape id;  MOVE */
    long         offset;          /* ... and the target cell, relative to the head          */
    Operand     *src;             /* WRITE: the value to store                              */
    int          dir;             /* MOVE: -1 = L, +1 = R                                   */

    Cond        *cond;            /* IF                                   */
    struct Stmt *then_body;       /* IF                                   */
    struct Stmt *else_body;       /* IF: NULL when there is no else       */

    Operand     *cells;           /* ON: the cells inspected (linked list) */
    int          ncells;          /* ON: how many                          */
    OnRow       *rows;            /* ON: the rule rows, in source order    */

    int          state;           /* TRANSITION: target state id          */

    int          line;            /* source line, for runtime errors      */
} Stmt;

/* STATE TABLE 
 * `transition` may mention a state defined LATER in the file, so names get an id the first time they are seen (state_intern).
 * The body is filled in when `state Name { ... }` is parsed (state_define).
 */
#define MAX_STATES 256

typedef struct {
    char *name;
    Stmt *body;        /* NULL for an empty body */
    int   defined;     /* 1 once a `state Name { ... }` has been parsed */
    int   line;        /* line of the definition (or of the first use, if never defined) */
} State;

extern State states[MAX_STATES];
extern int   num_states;
extern int   start_state;      /* explicit `start state`, else the first state defined; -1 if none */

int  state_intern(const char *name);          /* id for name (created if new)   */
int  state_define(int id, Stmt *body);        /* 0 = ok, -1 = defined twice     */
int  state_mark_start(int id);                /* 0 = ok, -1 = a start state was already chosen */
int  state_first_undefined(void);             /* id of a used-but-undefined state, or -1 */

/*  CONSTRUCTORS 
 * Called from parser.y. Nodes are never freed: the program exits after running.
 */
Operand *operand_lit   (int value);
Operand *operand_cell  (int tape, long offset);
Operand *operand_append(Operand *list, Operand *o);
int      operand_count (const Operand *list);

Cond *cond_cmp(CmpOp op, Operand *a, Operand *b);
Cond *cond_and(Cond *l, Cond *r);
Cond *cond_or (Cond *l, Cond *r);
Cond *cond_not(Cond *c);

Pattern *pattern_value (int value);
Pattern *pattern_any   (void);
Pattern *pattern_append(Pattern *list, Pattern *p);
int      pattern_count (const Pattern *list);

OnRow *onrow_new   (Pattern *pats, Stmt *body);
OnRow *onrow_append(OnRow *list, OnRow *row);

Stmt *stmt_write     (int tape, long offset, Operand *src);
Stmt *stmt_move      (int tape, int dir);
Stmt *stmt_inc       (int tape, long offset);
Stmt *stmt_dec       (int tape, long offset);
Stmt *stmt_if        (Cond *cond, Stmt *then_body, Stmt *else_body);
Stmt *stmt_on        (Operand *cells, OnRow *rows);
Stmt *stmt_transition(int state);
Stmt *stmt_halt      (void);
Stmt *stmt_accept    (void);
Stmt *stmt_reject    (void);
Stmt *stmt_print     (int tape, long offset);
Stmt *stmt_printn    (int tape, long offset);
Stmt *stmt_input     (int tape, long offset);

/* Append `s` to the end of list `list` (list may be NULL). Returns the head. */
Stmt *stmt_append(Stmt *list, Stmt *s);

#endif
