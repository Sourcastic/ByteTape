/*
 * main.c -- command line entry point.
 *
 *   ./bytetape [-q] [--trace] [--max-steps N] program.bt
 *
 * Steps: parse the file (building the AST), check the program, run it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "interp.h"

extern FILE *yyin;
int  yyparse(void);
extern int parse_errors;          /* counted in parser.y */

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [options] program.bt\n"
            "  -q               quiet: do not print the final tapes when the program stops\n"
            "  --trace          print the state and every tape before each step\n"
            "  --max-steps N    stop with an error after N steps (catches endless loops)\n"
            "  -h, --help       show this message\n"
            "\n"
            "exit codes: 0 halt/accept, 1 reject, 2 usage, 3 compile error, 4 runtime error\n",
            prog);
}

int main(int argc, char **argv)
{
    RunOptions  opts = { 1, 0, 0 };
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0) {
            opts.dump_tapes = 0;
        } else if (strcmp(argv[i], "--trace") == 0) {
            opts.trace = 1;
        } else if (strcmp(argv[i], "--max-steps") == 0) {
            char *end;
            if (++i >= argc) { usage(argv[0]); return EXIT_USAGE; }
            opts.max_steps = strtol(argv[i], &end, 10);
            if (*end || opts.max_steps <= 0) {
                fprintf(stderr, "--max-steps needs a positive number\n");
                return EXIT_USAGE;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return EXIT_OK;
        } else if (argv[i][0] == '-' || path) {
            usage(argv[0]);
            return EXIT_USAGE;
        } else {
            path = argv[i];
        }
    }
    if (!path) {
        usage(argv[0]);
        return EXIT_USAGE;
    }

    yyin = fopen(path, "r");
    if (!yyin) {
        perror(path);
        return EXIT_USAGE;
    }

    /* 1. parse: builds tapes, the state table and the AST */
    int syntax_failed = yyparse();
    fclose(yyin);

    /* 2. check: every state that is used must be defined */
    if (!syntax_failed) {
        int u = state_first_undefined();
        if (u >= 0) {
            fprintf(stderr, "line %d: error: state '%s' is used but never defined\n",
                    states[u].line, states[u].name);
            parse_errors++;
        } else if (start_state < 0 && parse_errors == 0) {
            fprintf(stderr, "error: the program defines no states\n");
            parse_errors++;
        }
    }
    if (syntax_failed || parse_errors > 0) {
        fprintf(stderr, "%s: not run (errors found)\n", path);
        return EXIT_COMPILE;
    }

    /* 3. run */
    return run_program(&opts);
}
