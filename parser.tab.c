/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 19 "parser.y"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "ast.h"
#include "tape.h"
#include "codegen.h"
#include "interp.h"

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

#line 214 "parser.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "parser.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_NUMBER = 3,                     /* NUMBER  */
  YYSYMBOL_IDENT = 4,                      /* IDENT  */
  YYSYMBOL_SEC_TAPES = 5,                  /* "%% tapes"  */
  YYSYMBOL_SEC_STATES = 6,                 /* "%% states"  */
  YYSYMBOL_BAD_SECTION = 7,                /* "bad section marker"  */
  YYSYMBOL_STATE = 8,                      /* STATE  */
  YYSYMBOL_START = 9,                      /* START  */
  YYSYMBOL_TRANSITION = 10,                /* TRANSITION  */
  YYSYMBOL_HALT = 11,                      /* HALT  */
  YYSYMBOL_KW_ACCEPT = 12,                 /* KW_ACCEPT  */
  YYSYMBOL_KW_REJECT = 13,                 /* KW_REJECT  */
  YYSYMBOL_WRITE = 14,                     /* WRITE  */
  YYSYMBOL_MOVE = 15,                      /* MOVE  */
  YYSYMBOL_INC = 16,                       /* INC  */
  YYSYMBOL_DEC = 17,                       /* DEC  */
  YYSYMBOL_IF = 18,                        /* IF  */
  YYSYMBOL_ELSE = 19,                      /* ELSE  */
  YYSYMBOL_ON = 20,                        /* ON  */
  YYSYMBOL_AND = 21,                       /* AND  */
  YYSYMBOL_OR = 22,                        /* OR  */
  YYSYMBOL_NOT = 23,                       /* NOT  */
  YYSYMBOL_PRINT = 24,                     /* PRINT  */
  YYSYMBOL_PRINTN = 25,                    /* PRINTN  */
  YYSYMBOL_INPUT = 26,                     /* INPUT  */
  YYSYMBOL_LEFT = 27,                      /* LEFT  */
  YYSYMBOL_RIGHT = 28,                     /* RIGHT  */
  YYSYMBOL_ARROW = 29,                     /* ARROW  */
  YYSYMBOL_EQ = 30,                        /* EQ  */
  YYSYMBOL_NE = 31,                        /* NE  */
  YYSYMBOL_LE = 32,                        /* LE  */
  YYSYMBOL_GE = 33,                        /* GE  */
  YYSYMBOL_34_ = 34,                       /* ':'  */
  YYSYMBOL_35_ = 35,                       /* '{'  */
  YYSYMBOL_36_ = 36,                       /* '}'  */
  YYSYMBOL_37_ = 37,                       /* ','  */
  YYSYMBOL_38_ = 38,                       /* '*'  */
  YYSYMBOL_39_ = 39,                       /* '['  */
  YYSYMBOL_40_ = 40,                       /* ']'  */
  YYSYMBOL_41_ = 41,                       /* '+'  */
  YYSYMBOL_42_ = 42,                       /* '-'  */
  YYSYMBOL_43_ = 43,                       /* '('  */
  YYSYMBOL_44_ = 44,                       /* ')'  */
  YYSYMBOL_45_ = 45,                       /* '<'  */
  YYSYMBOL_46_ = 46,                       /* '>'  */
  YYSYMBOL_YYACCEPT = 47,                  /* $accept  */
  YYSYMBOL_program = 48,                   /* program  */
  YYSYMBOL_tape_decls = 49,                /* tape_decls  */
  YYSYMBOL_tape_decl = 50,                 /* tape_decl  */
  YYSYMBOL_init_list_opt = 51,             /* init_list_opt  */
  YYSYMBOL_init_list = 52,                 /* init_list  */
  YYSYMBOL_state_defs = 53,                /* state_defs  */
  YYSYMBOL_state_def = 54,                 /* state_def  */
  YYSYMBOL_stmt_list = 55,                 /* stmt_list  */
  YYSYMBOL_block = 56,                     /* block  */
  YYSYMBOL_stmt = 57,                      /* stmt  */
  YYSYMBOL_dir = 58,                       /* dir  */
  YYSYMBOL_if_stmt = 59,                   /* if_stmt  */
  YYSYMBOL_on_stmt = 60,                   /* on_stmt  */
  YYSYMBOL_cell_list = 61,                 /* cell_list  */
  YYSYMBOL_row_list = 62,                  /* row_list  */
  YYSYMBOL_row = 63,                       /* row  */
  YYSYMBOL_pattern_list = 64,              /* pattern_list  */
  YYSYMBOL_pattern = 65,                   /* pattern  */
  YYSYMBOL_cell_ref = 66,                  /* cell_ref  */
  YYSYMBOL_operand = 67,                   /* operand  */
  YYSYMBOL_cond = 68,                      /* cond  */
  YYSYMBOL_relop = 69                      /* relop  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if 1

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* 1 */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  4
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   96

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  47
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  23
/* YYNRULES -- Number of rules.  */
#define YYNRULES  60
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  107

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   288


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      43,    44,    38,    41,    37,    42,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    34,     2,
      45,     2,    46,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    39,     2,    40,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    35,     2,    36,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   198,   198,   211,   212,   216,   232,   233,   237,   238,
     244,   245,   249,   250,   254,   255,   259,   265,   266,   267,
     268,   269,   270,   271,   272,   273,   274,   275,   276,   277,
     281,   282,   286,   287,   288,   294,   298,   299,   303,   304,
     308,   312,   313,   317,   318,   325,   326,   327,   331,   332,
     336,   337,   338,   339,   340,   344,   345,   346,   347,   348,
     349
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if 1
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "NUMBER", "IDENT",
  "\"%% tapes\"", "\"%% states\"", "\"bad section marker\"", "STATE",
  "START", "TRANSITION", "HALT", "KW_ACCEPT", "KW_REJECT", "WRITE", "MOVE",
  "INC", "DEC", "IF", "ELSE", "ON", "AND", "OR", "NOT", "PRINT", "PRINTN",
  "INPUT", "LEFT", "RIGHT", "ARROW", "EQ", "NE", "LE", "GE", "':'", "'{'",
  "'}'", "','", "'*'", "'['", "']'", "'+'", "'-'", "'('", "')'", "'<'",
  "'>'", "$accept", "program", "tape_decls", "tape_decl", "init_list_opt",
  "init_list", "state_defs", "state_def", "stmt_list", "block", "stmt",
  "dir", "if_stmt", "on_stmt", "cell_list", "row_list", "row",
  "pattern_list", "pattern", "cell_ref", "operand", "cond", "relop", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-35)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int8 yypact[] =
{
      19,   -35,    37,    56,   -35,    10,   -35,   -35,    12,    61,
      60,    63,    67,   -35,   -35,    40,    42,    45,    73,   -35,
      78,   -35,   -35,    45,   -35,     3,   -35,    79,   -35,   -35,
     -35,    43,    80,    43,    43,    -1,    43,    43,    43,    43,
     -35,   -35,   -35,   -35,   -35,    81,     2,    46,   -35,   -35,
     -35,    -1,    -1,   -35,    26,    30,    31,   -35,   -35,   -35,
     -35,    -6,   -35,   -35,   -35,   -35,   -35,     4,   -35,   -35,
     -35,   -35,   -35,   -35,     2,    -1,    -1,    68,     8,    43,
     -35,    83,    85,   -35,   -35,   -35,    69,    15,   -35,   -35,
       7,   -35,    24,   -35,   -35,    49,    51,   -35,   -35,   -35,
     -35,    45,     8,   -35,   -35,   -35,   -35
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     3,     0,     0,     1,     0,    10,     4,     0,     2,
       6,     0,     0,    11,     8,     0,     7,     0,     0,     5,
       0,    14,    12,     0,     9,     0,    13,     0,    25,    26,
      27,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      16,    15,    28,    29,    24,     0,     0,     0,    19,    20,
      48,     0,     0,    49,     0,     0,     0,    36,    21,    22,
      23,     0,    17,    30,    31,    18,    53,     0,    55,    56,
      59,    60,    57,    58,     0,     0,     0,    32,     0,     0,
      45,     0,     0,    54,    50,    51,    52,     0,    43,    44,
       0,    38,     0,    41,    37,     0,     0,    33,    34,    35,
      39,     0,     0,    46,    47,    40,    42
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -35,   -35,   -35,   -35,   -35,   -35,   -35,   -35,   -35,   -23,
     -35,   -35,     5,   -35,   -35,   -35,     6,   -35,    -9,   -30,
     -34,   -21,   -35
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,     2,     3,     7,    15,    16,     9,    13,    25,    22,
      41,    65,    42,    43,    56,    90,    91,    92,    93,    53,
      54,    55,    74
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
      26,    46,    50,    48,    49,    50,    57,    58,    59,    60,
      88,    88,    62,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    51,    36,     1,    75,    76,    37,    38,    39,
      66,    67,    77,    35,    80,    81,    82,     4,    45,    40,
      84,    45,    52,    99,     8,    89,    89,    10,    83,    94,
      21,    75,    76,   101,    85,    86,    68,    69,    70,    71,
       5,   102,     6,    14,    97,    21,    78,    17,    79,    11,
      12,    72,    73,    63,    64,    18,    19,    23,   105,    20,
      21,    24,    45,    44,    47,    61,    95,    87,    96,   103,
      75,   104,    98,   106,     0,     0,   100
};

static const yytype_int8 yycheck[] =
{
      23,    31,     3,    33,    34,     3,    36,    37,    38,    39,
       3,     3,    46,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    23,    20,     5,    21,    22,    24,    25,    26,
      51,    52,    55,    18,    40,    41,    42,     0,    39,    36,
      74,    39,    43,    36,    34,    38,    38,    35,    44,    79,
      35,    21,    22,    29,    75,    76,    30,    31,    32,    33,
       4,    37,     6,     3,    87,    35,    35,     4,    37,     8,
       9,    45,    46,    27,    28,     8,    36,     4,   101,    37,
      35,     3,    39,     4,     4,     4,     3,    19,     3,    40,
      21,    40,    87,   102,    -1,    -1,    90
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     5,    48,    49,     0,     4,     6,    50,    34,    53,
      35,     8,     9,    54,     3,    51,    52,     4,     8,    36,
      37,    35,    56,     4,     3,    55,    56,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    20,    24,    25,    26,
      36,    57,    59,    60,     4,    39,    66,     4,    66,    66,
       3,    23,    43,    66,    67,    68,    61,    66,    66,    66,
      66,     4,    67,    27,    28,    58,    68,    68,    30,    31,
      32,    33,    45,    46,    69,    21,    22,    56,    35,    37,
      40,    41,    42,    44,    67,    68,    68,    19,     3,    38,
      62,    63,    64,    65,    66,     3,     3,    56,    59,    36,
      63,    29,    37,    40,    40,    56,    65
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    47,    48,    49,    49,    50,    51,    51,    52,    52,
      53,    53,    54,    54,    55,    55,    56,    57,    57,    57,
      57,    57,    57,    57,    57,    57,    57,    57,    57,    57,
      58,    58,    59,    59,    59,    60,    61,    61,    62,    62,
      63,    64,    64,    65,    65,    66,    66,    66,    67,    67,
      68,    68,    68,    68,    68,    69,    69,    69,    69,    69,
      69
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     4,     0,     2,     5,     0,     1,     1,     3,
       0,     2,     3,     4,     0,     2,     3,     3,     3,     2,
       2,     2,     2,     2,     2,     1,     1,     1,     1,     1,
       1,     1,     3,     5,     5,     5,     1,     3,     1,     2,
       3,     1,     3,     1,     1,     3,     5,     5,     1,     1,
       3,     3,     3,     2,     3,     1,     1,     1,     1,     1,
       1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


/* Context of a parse error.  */
typedef struct
{
  yy_state_t *yyssp;
  yysymbol_kind_t yytoken;
} yypcontext_t;

/* Put in YYARG at most YYARGN of the expected tokens given the
   current YYCTX, and return the number of tokens stored in YYARG.  If
   YYARG is null, return the number of expected tokens (guaranteed to
   be less than YYNTOKENS).  Return YYENOMEM on memory exhaustion.
   Return 0 if there are more than YYARGN expected tokens, yet fill
   YYARG up to YYARGN. */
static int
yypcontext_expected_tokens (const yypcontext_t *yyctx,
                            yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  int yyn = yypact[+*yyctx->yyssp];
  if (!yypact_value_is_default (yyn))
    {
      /* Start YYX at -YYN if negative to avoid negative indexes in
         YYCHECK.  In other words, skip the first -YYN actions for
         this state because they are default actions.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;
      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yyx;
      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
        if (yycheck[yyx + yyn] == yyx && yyx != YYSYMBOL_YYerror
            && !yytable_value_is_error (yytable[yyx + yyn]))
          {
            if (!yyarg)
              ++yycount;
            else if (yycount == yyargn)
              return 0;
            else
              yyarg[yycount++] = YY_CAST (yysymbol_kind_t, yyx);
          }
    }
  if (yyarg && yycount == 0 && 0 < yyargn)
    yyarg[0] = YYSYMBOL_YYEMPTY;
  return yycount;
}




#ifndef yystrlen
# if defined __GLIBC__ && defined _STRING_H
#  define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
# else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
# endif
#endif

#ifndef yystpcpy
# if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#  define yystpcpy stpcpy
# else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
# endif
#endif

#ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
      char const *yyp = yystr;
      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
#endif


static int
yy_syntax_error_arguments (const yypcontext_t *yyctx,
                           yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yyctx->yytoken != YYSYMBOL_YYEMPTY)
    {
      int yyn;
      if (yyarg)
        yyarg[yycount] = yyctx->yytoken;
      ++yycount;
      yyn = yypcontext_expected_tokens (yyctx,
                                        yyarg ? yyarg + 1 : yyarg, yyargn - 1);
      if (yyn == YYENOMEM)
        return YYENOMEM;
      else
        yycount += yyn;
    }
  return yycount;
}

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return -1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return YYENOMEM if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                const yypcontext_t *yyctx)
{
  enum { YYARGS_MAX = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  yysymbol_kind_t yyarg[YYARGS_MAX];
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* Actual size of YYARG. */
  int yycount = yy_syntax_error_arguments (yyctx, yyarg, YYARGS_MAX);
  if (yycount == YYENOMEM)
    return YYENOMEM;

  switch (yycount)
    {
#define YYCASE_(N, S)                       \
      case N:                               \
        yyformat = S;                       \
        break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
    }

  /* Compute error message size.  Don't count the "%s"s, but reserve
     room for the terminator.  */
  yysize = yystrlen (yyformat) - 2 * yycount + 1;
  {
    int yyi;
    for (yyi = 0; yyi < yycount; ++yyi)
      {
        YYPTRDIFF_T yysize1
          = yysize + yytnamerr (YY_NULLPTR, yytname[yyarg[yyi]]);
        if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
          yysize = yysize1;
        else
          return YYENOMEM;
      }
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return -1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yytname[yyarg[yyi++]]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* program: "%% tapes" tape_decls "%% states" state_defs  */
#line 199 "parser.y"
        {
            /* Phase 2: the whole program has been parsed. If we are in compiler mode
             * (main.c set codegen_out) and the program is free of errors, emit LLVM IR. */
            if (codegen_out && parse_errors == 0
                && start_state >= 0 && state_first_undefined() < 0)
                codegen_emit(codegen_out);
        }
#line 1613 "parser.tab.c"
    break;

  case 5: /* tape_decl: IDENT ':' '{' init_list_opt '}'  */
#line 217 "parser.y"
        {
            int id = tape_declare((yyvsp[-4].str));
            if (id < 0)
                semantic_error("tape '%s' is declared twice", (yyvsp[-4].str));
            else
            {
                tape_set_initial(id, init_buf, init_n);
                cg_tape_init(id, init_buf, init_n);     /* remembered for code generation */
            }
            init_n = 0;
            free((yyvsp[-4].str));
        }
#line 1630 "parser.tab.c"
    break;

  case 8: /* init_list: NUMBER  */
#line 237 "parser.y"
                                { init_push((yyvsp[0].num)); }
#line 1636 "parser.tab.c"
    break;

  case 9: /* init_list: init_list ',' NUMBER  */
#line 238 "parser.y"
                                { init_push((yyvsp[0].num)); }
#line 1642 "parser.tab.c"
    break;

  case 12: /* state_def: STATE IDENT block  */
#line 249 "parser.y"
                                    { define_state((yyvsp[-1].str), (yyvsp[0].stmt), 0); }
#line 1648 "parser.tab.c"
    break;

  case 13: /* state_def: START STATE IDENT block  */
#line 250 "parser.y"
                                    { define_state((yyvsp[-1].str), (yyvsp[0].stmt), 1); }
#line 1654 "parser.tab.c"
    break;

  case 14: /* stmt_list: %empty  */
#line 254 "parser.y"
                                { (yyval.stmt) = NULL; }
#line 1660 "parser.tab.c"
    break;

  case 15: /* stmt_list: stmt_list stmt  */
#line 255 "parser.y"
                                { (yyval.stmt) = stmt_append((yyvsp[-1].stmt), (yyvsp[0].stmt)); }
#line 1666 "parser.tab.c"
    break;

  case 16: /* block: '{' stmt_list '}'  */
#line 259 "parser.y"
                                { (yyval.stmt) = (yyvsp[-1].stmt); }
#line 1672 "parser.tab.c"
    break;

  case 17: /* stmt: WRITE cell_ref operand  */
#line 265 "parser.y"
                                { (yyval.stmt) = stmt_write((yyvsp[-1].opd)->tape, (yyvsp[-1].opd)->offset, (yyvsp[0].opd)); free((yyvsp[-1].opd)); }
#line 1678 "parser.tab.c"
    break;

  case 18: /* stmt: MOVE IDENT dir  */
#line 266 "parser.y"
                                { (yyval.stmt) = stmt_move(lookup_tape((yyvsp[-1].str)), (yyvsp[0].num)); }
#line 1684 "parser.tab.c"
    break;

  case 19: /* stmt: INC cell_ref  */
#line 267 "parser.y"
                                { (yyval.stmt) = cell_stmt(stmt_inc,    (yyvsp[0].opd)); }
#line 1690 "parser.tab.c"
    break;

  case 20: /* stmt: DEC cell_ref  */
#line 268 "parser.y"
                                { (yyval.stmt) = cell_stmt(stmt_dec,    (yyvsp[0].opd)); }
#line 1696 "parser.tab.c"
    break;

  case 21: /* stmt: PRINT cell_ref  */
#line 269 "parser.y"
                                { (yyval.stmt) = cell_stmt(stmt_print,  (yyvsp[0].opd)); }
#line 1702 "parser.tab.c"
    break;

  case 22: /* stmt: PRINTN cell_ref  */
#line 270 "parser.y"
                                { (yyval.stmt) = cell_stmt(stmt_printn, (yyvsp[0].opd)); }
#line 1708 "parser.tab.c"
    break;

  case 23: /* stmt: INPUT cell_ref  */
#line 271 "parser.y"
                                { (yyval.stmt) = cell_stmt(stmt_input,  (yyvsp[0].opd)); }
#line 1714 "parser.tab.c"
    break;

  case 24: /* stmt: TRANSITION IDENT  */
#line 272 "parser.y"
                                { (yyval.stmt) = stmt_transition(state_intern((yyvsp[0].str))); free((yyvsp[0].str)); }
#line 1720 "parser.tab.c"
    break;

  case 25: /* stmt: HALT  */
#line 273 "parser.y"
                                { (yyval.stmt) = stmt_halt(); }
#line 1726 "parser.tab.c"
    break;

  case 26: /* stmt: KW_ACCEPT  */
#line 274 "parser.y"
                                { (yyval.stmt) = stmt_accept(); }
#line 1732 "parser.tab.c"
    break;

  case 27: /* stmt: KW_REJECT  */
#line 275 "parser.y"
                                { (yyval.stmt) = stmt_reject(); }
#line 1738 "parser.tab.c"
    break;

  case 28: /* stmt: if_stmt  */
#line 276 "parser.y"
                                { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1744 "parser.tab.c"
    break;

  case 29: /* stmt: on_stmt  */
#line 277 "parser.y"
                                { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1750 "parser.tab.c"
    break;

  case 30: /* dir: LEFT  */
#line 281 "parser.y"
                                { (yyval.num) = -1; }
#line 1756 "parser.tab.c"
    break;

  case 31: /* dir: RIGHT  */
#line 282 "parser.y"
                                { (yyval.num) =  1; }
#line 1762 "parser.tab.c"
    break;

  case 32: /* if_stmt: IF cond block  */
#line 286 "parser.y"
                                        { (yyval.stmt) = stmt_if((yyvsp[-1].cond), (yyvsp[0].stmt), NULL); }
#line 1768 "parser.tab.c"
    break;

  case 33: /* if_stmt: IF cond block ELSE block  */
#line 287 "parser.y"
                                        { (yyval.stmt) = stmt_if((yyvsp[-3].cond), (yyvsp[-2].stmt), (yyvsp[0].stmt)); }
#line 1774 "parser.tab.c"
    break;

  case 34: /* if_stmt: IF cond block ELSE if_stmt  */
#line 288 "parser.y"
                                        { (yyval.stmt) = stmt_if((yyvsp[-3].cond), (yyvsp[-2].stmt), (yyvsp[0].stmt)); }
#line 1780 "parser.tab.c"
    break;

  case 35: /* on_stmt: ON cell_list '{' row_list '}'  */
#line 294 "parser.y"
                                        { (yyval.stmt) = make_on((yyvsp[-3].opd), (yyvsp[-1].row)); }
#line 1786 "parser.tab.c"
    break;

  case 36: /* cell_list: cell_ref  */
#line 298 "parser.y"
                                        { (yyval.opd) = (yyvsp[0].opd); }
#line 1792 "parser.tab.c"
    break;

  case 37: /* cell_list: cell_list ',' cell_ref  */
#line 299 "parser.y"
                                        { (yyval.opd) = operand_append((yyvsp[-2].opd), (yyvsp[0].opd)); }
#line 1798 "parser.tab.c"
    break;

  case 38: /* row_list: row  */
#line 303 "parser.y"
                                        { (yyval.row) = (yyvsp[0].row); }
#line 1804 "parser.tab.c"
    break;

  case 39: /* row_list: row_list row  */
#line 304 "parser.y"
                                        { (yyval.row) = onrow_append((yyvsp[-1].row), (yyvsp[0].row)); }
#line 1810 "parser.tab.c"
    break;

  case 40: /* row: pattern_list ARROW block  */
#line 308 "parser.y"
                                        { (yyval.row) = onrow_new((yyvsp[-2].pat), (yyvsp[0].stmt)); }
#line 1816 "parser.tab.c"
    break;

  case 41: /* pattern_list: pattern  */
#line 312 "parser.y"
                                        { (yyval.pat) = (yyvsp[0].pat); }
#line 1822 "parser.tab.c"
    break;

  case 42: /* pattern_list: pattern_list ',' pattern  */
#line 313 "parser.y"
                                        { (yyval.pat) = pattern_append((yyvsp[-2].pat), (yyvsp[0].pat)); }
#line 1828 "parser.tab.c"
    break;

  case 43: /* pattern: NUMBER  */
#line 317 "parser.y"
                                        { (yyval.pat) = pattern_value(check_lit((yyvsp[0].num))); }
#line 1834 "parser.tab.c"
    break;

  case 44: /* pattern: '*'  */
#line 318 "parser.y"
                                        { (yyval.pat) = pattern_any(); }
#line 1840 "parser.tab.c"
    break;

  case 45: /* cell_ref: '[' IDENT ']'  */
#line 325 "parser.y"
                                    { (yyval.opd) = operand_cell(lookup_tape((yyvsp[-1].str)), 0); }
#line 1846 "parser.tab.c"
    break;

  case 46: /* cell_ref: '[' IDENT '+' NUMBER ']'  */
#line 326 "parser.y"
                                    { (yyval.opd) = operand_cell(lookup_tape((yyvsp[-3].str)),  (yyvsp[-1].num)); }
#line 1852 "parser.tab.c"
    break;

  case 47: /* cell_ref: '[' IDENT '-' NUMBER ']'  */
#line 327 "parser.y"
                                    { (yyval.opd) = operand_cell(lookup_tape((yyvsp[-3].str)), -(yyvsp[-1].num)); }
#line 1858 "parser.tab.c"
    break;

  case 48: /* operand: NUMBER  */
#line 331 "parser.y"
                                { (yyval.opd) = operand_lit(check_lit((yyvsp[0].num))); }
#line 1864 "parser.tab.c"
    break;

  case 49: /* operand: cell_ref  */
#line 332 "parser.y"
                                { (yyval.opd) = (yyvsp[0].opd); }
#line 1870 "parser.tab.c"
    break;

  case 50: /* cond: operand relop operand  */
#line 336 "parser.y"
                                { (yyval.cond) = cond_cmp((CmpOp)(yyvsp[-1].num), (yyvsp[-2].opd), (yyvsp[0].opd)); }
#line 1876 "parser.tab.c"
    break;

  case 51: /* cond: cond AND cond  */
#line 337 "parser.y"
                                { (yyval.cond) = cond_and((yyvsp[-2].cond), (yyvsp[0].cond)); }
#line 1882 "parser.tab.c"
    break;

  case 52: /* cond: cond OR cond  */
#line 338 "parser.y"
                                { (yyval.cond) = cond_or((yyvsp[-2].cond), (yyvsp[0].cond)); }
#line 1888 "parser.tab.c"
    break;

  case 53: /* cond: NOT cond  */
#line 339 "parser.y"
                                { (yyval.cond) = cond_not((yyvsp[0].cond)); }
#line 1894 "parser.tab.c"
    break;

  case 54: /* cond: '(' cond ')'  */
#line 340 "parser.y"
                                { (yyval.cond) = (yyvsp[-1].cond); }
#line 1900 "parser.tab.c"
    break;

  case 55: /* relop: EQ  */
#line 344 "parser.y"
                                { (yyval.num) = CMP_EQ; }
#line 1906 "parser.tab.c"
    break;

  case 56: /* relop: NE  */
#line 345 "parser.y"
                                { (yyval.num) = CMP_NE; }
#line 1912 "parser.tab.c"
    break;

  case 57: /* relop: '<'  */
#line 346 "parser.y"
                                { (yyval.num) = CMP_LT; }
#line 1918 "parser.tab.c"
    break;

  case 58: /* relop: '>'  */
#line 347 "parser.y"
                                { (yyval.num) = CMP_GT; }
#line 1924 "parser.tab.c"
    break;

  case 59: /* relop: LE  */
#line 348 "parser.y"
                                { (yyval.num) = CMP_LE; }
#line 1930 "parser.tab.c"
    break;

  case 60: /* relop: GE  */
#line 349 "parser.y"
                                { (yyval.num) = CMP_GE; }
#line 1936 "parser.tab.c"
    break;


#line 1940 "parser.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      {
        yypcontext_t yyctx
          = {yyssp, yytoken};
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == -1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *,
                             YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (yymsg)
              {
                yysyntax_error_status
                  = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
                yymsgp = yymsg;
              }
            else
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = YYENOMEM;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == YYENOMEM)
          YYNOMEM;
      }
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
  return yyresult;
}

#line 352 "parser.y"


void yyerror(const char *msg)
{
    fprintf(stderr, "line %d: %s\n", yylineno, msg);
    parse_errors++;
}
