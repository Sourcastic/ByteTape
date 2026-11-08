/*interp.h -- runs a parsed ByteTape program by walking the AST. */
#ifndef INTERP_H
#define INTERP_H

typedef struct {
    int  dump_tapes;     /* print the final tapes when the program stops */
    int  trace;          /* print state + tapes at every step            */
    long max_steps;      /* stop with an error after this many steps; 0 = no limit */
} RunOptions;

enum {
    EXIT_OK       = 0,
    EXIT_REJECT   = 1,
    EXIT_USAGE    = 2,   /* bad command line / cannot open the file    */
    EXIT_COMPILE  = 3,   /* syntax or semantic errors; nothing was run */
    EXIT_RUNTIME  = 4 
};

int run_program(const RunOptions *opts);

#endif
