/*
 * codegen.c -- ByteTape -> LLVM IR (Phase 2).
 *
 * Walks the same AST that interp.c executes and prints a textual LLVM module (.ll). Nothing is executed at compile time.
 * 
 *   define i32 @main(i32 %argc, ptr %argv) {
 *   entry:       declare the tapes, load their initial contents, br to the start state
 *   S_<name>:    one basic block (or several) for every ByteTape state
 *   EXIT_HALT / EXIT_ACCEPT / EXIT_REJECT:   leave the program with the right exit code
 *   }
 *
 *   A `transition X` is simply `br label %S_X`.  Because a state is a basic
 *   block and a transition is a jump, the whole Turing machine becomes a
 *   control-flow graph -- no interpreter loop is left in the output.
 *
 * Values
 * ------
 *   Every cell value travels through the IR as an i32 in the range 0-255.
 *   Cells are read/written through the runtime (rt.c -> tape.c); writing
 *   truncates to 8 bits.  inc/dec are done on a real i8, so the 255 -> 0 and
 *   0 -> 255 wrap-around is plain LLVM `add i8` behaviour.
 *
 * Naming rules for the output
 * ---------------------------
 *   %t<N>   temporaries (each used inside one basic block only, so no phi nodes)
 *   L<N>    anonymous labels made by if / on
 *   S_<id>  a state's entry label
 *   @.str.N / @.tinit.N   string / tape-initialisation constants (printed at the end)
 *
 * Requires LLVM 15 or newer (opaque pointers: the type `ptr`).
 */
#include "codegen.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "tape.h"

FILE *codegen_out = NULL;

/*Tape initial contents (recorded by the parser) */

static uint8_t *g_init[MAX_TAPES];
static int      g_init_n[MAX_TAPES];

void cg_tape_init(int id, const uint8_t *vals, int n)
{
    if (id < 0 || id >= MAX_TAPES)
        return;
    free(g_init[id]);
    g_init[id]   = NULL;
    g_init_n[id] = 0;
    if (n > 0) {
        g_init[id] = malloc((size_t)n);
        if (!g_init[id]) { fprintf(stderr, "out of memory\n"); exit(1); }
        memcpy(g_init[id], vals, (size_t)n);
        g_init_n[id] = n;
    }
}

/* String constants
 * /Functions are printed while we walk the tree, but LLVM wants each string
 *  as a global. We collect them here and print them after the function.
 */

static char **g_strs     = NULL;
static int    g_nstrs    = 0;
static int    g_strs_cap = 0;

/* Returns the number N of the global @.str.N holding `s` (reusing equal strings). */
static int add_str(const char *s)
{
    for (int i = 0; i < g_nstrs; i++)
        if (strcmp(g_strs[i], s) == 0)
            return i;
    if (g_nstrs == g_strs_cap) {
        g_strs_cap = g_strs_cap ? g_strs_cap * 2 : 32;
        g_strs = realloc(g_strs, (size_t)g_strs_cap * sizeof *g_strs);
        if (!g_strs) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    g_strs[g_nstrs] = strdup(s);
    return g_nstrs++;
}

/* Print a byte string as the body of an LLVM  c"..."  literal (plus the \00 terminator). */
static void print_c_string(FILE *out, const uint8_t *s, size_t n, int terminate)
{
    fputs("c\"", out);
    for (size_t i = 0; i < n; i++) {
        uint8_t c = s[i];
        if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\')
            fputc(c, out);
        else
            fprintf(out, "\\%02X", c);
    }
    if (terminate)
        fputs("\\00", out);
    fputc('"', out);
}

/* Emission state */

static FILE *out;
static int   ntmp;            /* next %t number                                    */
static int   nlbl;            /* next L number                                     */
static int   terminated;      /* 1 = the current basic block already ended (br/unreachable) */
static int   cur_state;       /* state being emitted (for error messages)          */

static int new_tmp(void) { return ntmp++; }
static int new_lbl(void) { return nlbl++; }

/* Start a new numbered basic block. */
static void label(int l)
{
    fprintf(out, "L%d:\n", l);
    terminated = 0;
}

/* Statements written after a `transition`/`halt`/... in the same block can never
 * run, but LLVM does not allow instructions after a terminator. Open a fresh
 * (unreachable) block for them. */
static void open_block(void)
{
    if (terminated)
        label(new_lbl());
}

static void br(int l)
{
    fprintf(out, "  br label %%L%d\n", l);
    terminated = 1;
}

static void br_named(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void br_named(const char *fmt, ...)
{
    va_list ap;
    fputs("  br label %", out);
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
    terminated = 1;
}

/* Operands and conditions */

typedef struct { char s[32]; } Val;      /* an IR operand: "7" or "%t12" */

/* Emit the code to get an operand's value as an i32. */
static Val operand(const Operand *o)
{
    Val v;
    if (o->kind == OPD_LIT) {
        snprintf(v.s, sizeof v.s, "%d", o->value);
    } else {
        int t = new_tmp();
        fprintf(out, "  %%t%d = call i32 @rt_read(i32 %d, i64 %ld)\n", t, o->tape, o->offset);
        snprintf(v.s, sizeof v.s, "%%t%d", t);
    }
    return v;
}

static const char *icmp_pred(CmpOp op)
{
    switch (op) {
    case CMP_EQ: return "eq";
    case CMP_NE: return "ne";
    case CMP_LT: return "slt";     /* values are 0-255, so signed == unsigned here */
    case CMP_GT: return "sgt";
    case CMP_LE: return "sle";
    case CMP_GE: return "sge";
    }
    return "eq";
}

/* Emit code for a condition; returns the name of the resulting i1.
 * Conditions never change a tape, so evaluating both sides of and/or is safe. */
static Val cond(const Cond *c)
{
    Val r;
    int t;
    switch (c->kind) {
    case COND_CMP: {
        Val a = operand(c->a);
        Val b = operand(c->b);
        t = new_tmp();
        fprintf(out, "  %%t%d = icmp %s i32 %s, %s\n", t, icmp_pred(c->op), a.s, b.s);
        break;
    }
    case COND_AND: {
        Val a = cond(c->l);
        Val b = cond(c->r);
        t = new_tmp();
        fprintf(out, "  %%t%d = and i1 %s, %s\n", t, a.s, b.s);
        break;
    }
    case COND_OR: {
        Val a = cond(c->l);
        Val b = cond(c->r);
        t = new_tmp();
        fprintf(out, "  %%t%d = or i1 %s, %s\n", t, a.s, b.s);
        break;
    }
    case COND_NOT: {
        Val a = cond(c->l);
        t = new_tmp();
        fprintf(out, "  %%t%d = xor i1 %s, true\n", t, a.s);
        break;
    }
    default:
        t = new_tmp();
        fprintf(out, "  %%t%d = icmp eq i32 0, 0\n", t);
        break;
    }
    snprintf(r.s, sizeof r.s, "%%t%d", t);
    return r;
}

/*Statements */

static void block(const Stmt *s);

/* "[Name]" or "[Name + 1]" / "[Name - 2]" -- the same text interp.c prints in its errors. */
static void describe_cell(char *buf, size_t n, const Operand *c)
{
    const char *name = tape_name(c->tape);
    if (c->offset == 0)
        snprintf(buf, n, "[%s]", name);
    else
        snprintf(buf, n, "[%s %c %ld]", name, c->offset < 0 ? '-' : '+',
                 c->offset < 0 ? -c->offset : c->offset);
}

/* With a declared alphabet, check a freshly made symbol before it is stored. */
static void emit_symbol_check(int line, int tmp)
{
    if (tape_alphabet_active())
        fprintf(out, "  call void @rt_check_symbol(i32 %d, i32 %%t%d)\n", line, tmp);
}

/* inc / dec: read, do the arithmetic on a real i8 so it wraps, write back. */
static void emit_incdec(const Stmt *s, const char *op)
{
    int a = new_tmp(), b = new_tmp(), c = new_tmp(), d = new_tmp();
    fprintf(out, "  %%t%d = call i32 @rt_read(i32 %d, i64 %ld)\n", a, s->tape, s->offset);
    fprintf(out, "  %%t%d = trunc i32 %%t%d to i8\n", b, a);
    fprintf(out, "  %%t%d = %s i8 %%t%d, 1\n", c, op, b);
    fprintf(out, "  %%t%d = zext i8 %%t%d to i32\n", d, c);
    emit_symbol_check(s->line, d);
    fprintf(out, "  call void @rt_write(i32 %d, i64 %ld, i32 %%t%d)\n", s->tape, s->offset, d);
}

static void emit_if(const Stmt *s)
{
    Val c = cond(s->cond);
    int l_then = new_lbl();
    int l_else = s->else_body ? new_lbl() : -1;
    int l_end  = new_lbl();

    fprintf(out, "  br i1 %s, label %%L%d, label %%L%d\n", c.s, l_then,
            s->else_body ? l_else : l_end);
    terminated = 1;

    label(l_then);
    block(s->then_body);
    if (!terminated)
        br(l_end);

    if (s->else_body) {
        label(l_else);
        block(s->else_body);
        if (!terminated)
            br(l_end);
    }
    label(l_end);
}

/*
 *   on [A], [B] { p1, p2 -> {..}   p1, p2 -> {..} }
 *
 *   1. read every listed cell once  (so a row that moves a head cannot change
 *      which row was chosen -- same rule as the interpreter)
 *   2. a chain of tests, top to bottom; the first row whose patterns all match jumps to its body
 *   3. no row matched  -> call rt_no_match (runtime error, exit code 4)
 *   4. each row body ends by falling out to L_end, or leaves the state with a transition/halt/...
 */
static void emit_on(const Stmt *s)
{
    Val vals[MAX_ON_CELLS];
    int n = 0;

    for (const Operand *c = s->cells; c && n < MAX_ON_CELLS; c = c->next)
        vals[n++] = operand(c);

    int nrows = 0;
    for (const OnRow *r = s->rows; r; r = r->next)
        nrows++;

    int *row_lbl = malloc((size_t)(nrows ? nrows : 1) * sizeof *row_lbl);
    if (!row_lbl) { fprintf(stderr, "out of memory\n"); exit(1); }
    for (int k = 0; k < nrows; k++)
        row_lbl[k] = new_lbl();
    int l_nomatch = new_lbl();
    int l_end     = new_lbl();

    /* the chain of tests */
    int k = 0;
    for (const OnRow *r = s->rows; r; r = r->next, k++) {
        open_block();                       /* previous row ended with a jump */

        Val acc = { "" };
        int have = 0, i = 0;
        for (const Pattern *p = r->pats; p; p = p->next, i++) {
            if (p->any)
                continue;                   /* `*` always matches: no test needed */
            int t = new_tmp();
            fprintf(out, "  %%t%d = icmp eq i32 %s, %d\n", t, vals[i].s, p->value);
            if (!have) {
                snprintf(acc.s, sizeof acc.s, "%%t%d", t);
                have = 1;
            } else {
                int u = new_tmp();
                fprintf(out, "  %%t%d = and i1 %s, %%t%d\n", u, acc.s, t);
                snprintf(acc.s, sizeof acc.s, "%%t%d", u);
            }
        }

        if (!have) {                        /* all `*`: this row always matches */
            br(row_lbl[k]);
        } else {
            int is_last = (r->next == NULL);
            int next    = is_last ? l_nomatch : new_lbl();
            fprintf(out, "  br i1 %s, label %%L%d, label %%L%d\n", acc.s, row_lbl[k], next);
            terminated = 1;
            if (!is_last)
                label(next);
        }
    }

    /* when no row matched */
    if (!terminated)                        /* (an `on` always has a row, but stay valid) */
        br(l_nomatch);
    label(l_nomatch);
    {
        char desc[96];
        fprintf(out, "  call void (i32, ptr, i32, ...) @rt_no_match(i32 %d, ptr @.str.%d, i32 %d",
                s->line, add_str(states[cur_state].name), n);
        int i = 0;
        for (const Operand *c = s->cells; c && i < n; c = c->next, i++) {
            describe_cell(desc, sizeof desc, c);
            fprintf(out, ", ptr @.str.%d, i32 %s", add_str(desc), vals[i].s);
        }
        fputs(")\n  unreachable\n", out);
        terminated = 1;
    }

    /*row bodies */
    k = 0;
    for (const OnRow *r = s->rows; r; r = r->next, k++) {
        label(row_lbl[k]);
        block(r->body);
        if (!terminated)
            br(l_end);
    }
    label(l_end);
    free(row_lbl);
}

static void stmt(const Stmt *s)
{
    open_block();

    switch (s->kind) {
    case STMT_WRITE: {
        Val v = operand(s->src);
        fprintf(out, "  call void @rt_write(i32 %d, i64 %ld, i32 %s)\n", s->tape, s->offset, v.s);
        break;
    }
    case STMT_MOVE:
        fprintf(out, "  call void @rt_move(i32 %d, i32 %d)\n", s->tape, s->dir);
        break;

    case STMT_INC: emit_incdec(s, "add"); break;
    case STMT_DEC: emit_incdec(s, "sub"); break;

    case STMT_IF:  emit_if(s);  break;
    case STMT_ON:  emit_on(s);  break;

    case STMT_TRANSITION:
        br_named("S_%s", states[s->state].name);
        break;
    case STMT_HALT:   br_named("EXIT_HALT");   break;
    case STMT_ACCEPT: br_named("EXIT_ACCEPT"); break;
    case STMT_REJECT: br_named("EXIT_REJECT"); break;

    case STMT_PRINT: {
        int t = new_tmp();
        fprintf(out, "  %%t%d = call i32 @rt_read(i32 %d, i64 %ld)\n", t, s->tape, s->offset);
        fprintf(out, "  call void @rt_print(i32 %%t%d)\n", t);
        break;
    }
    case STMT_PRINTN: {
        int t = new_tmp();
        fprintf(out, "  %%t%d = call i32 @rt_read(i32 %d, i64 %ld)\n", t, s->tape, s->offset);
        fprintf(out, "  call void @rt_printn(i32 %%t%d)\n", t);
        break;
    }
    case STMT_INPUT: {
        int t = new_tmp();
        fprintf(out, "  %%t%d = call i32 @rt_input()\n", t);
        emit_symbol_check(s->line, t);
        fprintf(out, "  call void @rt_write(i32 %d, i64 %ld, i32 %%t%d)\n", s->tape, s->offset, t);
        break;
    }
    }
}

static void block(const Stmt *s)
{
    for (; s; s = s->next)
        stmt(s);
}

/*The whole module */

static void emit_prologue(void)
{
    fputs("; ByteTape -> LLVM IR (generated by the ByteTape compiler; do not edit)\n"
          "\n"
          "; --- tape table (tape.c) and runtime (rt.c) ---\n"
          "declare i32  @tape_declare(ptr)\n"
          "declare void @tape_set_initial(i32, ptr, i32)\n"
          "declare void @rt_init(i32, ptr)\n"
          "declare void @rt_alphabet(ptr, i32)\n"
          "declare void @rt_check_symbol(i32, i32)\n"
          "declare i32  @rt_read(i32, i64)\n"
          "declare void @rt_write(i32, i64, i32)\n"
          "declare void @rt_move(i32, i32)\n"
          "declare void @rt_print(i32)\n"
          "declare void @rt_printn(i32)\n"
          "declare i32  @rt_input()\n"
          "declare void @rt_no_match(i32, ptr, i32, ...) noreturn\n"
          "declare void @rt_state_end(i32, ptr) noreturn\n"
          "declare i32  @rt_finish(i32)\n"
          "\n", out);
}

static void emit_main(void)
{
    fputs("define i32 @main(i32 %argc, ptr %argv) {\n"
          "entry:\n"
          "  call void @rt_init(i32 %argc, ptr %argv)\n", out);

    /* declare the tapes in id order, so the ids at run time equal the ids used in this IR */
    int ntapes = tape_count();
    for (int i = 0; i < ntapes; i++) {
        fprintf(out, "  call i32 @tape_declare(ptr @.str.%d)\n", add_str(tape_name(i)));
        if (g_init_n[i] > 0)
            fprintf(out, "  call void @tape_set_initial(i32 %d, ptr @.tinit.%d, i32 %d)\n",
                    i, i, g_init_n[i]);
    }
    if (tape_alphabet_active()) {            /* hand the tape alphabet to the runtime */
        int n = 0;
        for (int v = 0; v < 256; v++)
            n += tape_in_tape_alphabet(v);
        fprintf(out, "  call void @rt_alphabet(ptr @.alpha, i32 %d)\n", n);
    }
    br_named("S_%s", states[start_state].name);

    /* one block per state */
    for (int id = 0; id < num_states; id++) {
        cur_state = id;
        fprintf(out, "\nS_%s:\n", states[id].name);
        terminated = 0;

        block(states[id].body);

        if (!terminated) {          /* the state ran off its end: runtime error */
            fprintf(out, "  call void @rt_state_end(i32 %d, ptr @.str.%d)\n  unreachable\n",
                    states[id].line, add_str(states[id].name));
            terminated = 1;
        }
    }

    fputs("\n"
          "EXIT_HALT:\n"
          "  %rh = call i32 @rt_finish(i32 0)\n"
          "  ret i32 %rh\n"
          "\n"
          "EXIT_ACCEPT:\n"
          "  %ra = call i32 @rt_finish(i32 1)\n"
          "  ret i32 %ra\n"
          "\n"
          "EXIT_REJECT:\n"
          "  %rr = call i32 @rt_finish(i32 2)\n"
          "  ret i32 %rr\n"
          "}\n", out);
}

static void emit_constants(void)
{
    fputs("\n; constants \n", out);

    for (int i = 0; i < tape_count(); i++) {
        if (g_init_n[i] > 0) {
            fprintf(out, "@.tinit.%d = private constant [%d x i8] ", i, g_init_n[i]);
            print_c_string(out, g_init[i], (size_t)g_init_n[i], 0);
            fputc('\n', out);
        }
    }
    if (tape_alphabet_active()) {            /* the tape alphabet Gamma, one byte per symbol */
        uint8_t syms[256];
        int n = 0;
        for (int v = 0; v < 256; v++)
            if (tape_in_tape_alphabet(v))
                syms[n++] = (uint8_t)v;
        fprintf(out, "@.alpha = private constant [%d x i8] ", n);
        print_c_string(out, syms, (size_t)n, 0);
        fputc('\n', out);
    }
    for (int i = 0; i < g_nstrs; i++) {
        size_t len = strlen(g_strs[i]);
        fprintf(out, "@.str.%d = private unnamed_addr constant [%zu x i8] ", i, len + 1);
        print_c_string(out, (const uint8_t *)g_strs[i], len, 1);
        fputc('\n', out);
    }
}

void codegen_emit(FILE *f)
{
    out  = f;
    ntmp = nlbl = 0;
    terminated = 0;

    emit_prologue();
    emit_main();
    emit_constants();      /* last, because emitting main collected the strings */
}
