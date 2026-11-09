CC     = gcc
CFLAGS = -std=gnu17 -Wall -Wextra -g
TARGET = bytetape

# The compiler / interpreter itself
SRCS   = parser.tab.c lex.yy.c ast.c interp.c codegen.c tape.c main.c
HDRS   = ast.h interp.h codegen.h tape.h

# The runtime that every compiled ByteTape program links against
RTLIB  = libbtrt.a

all: $(TARGET) $(RTLIB)
	@chmod +x bytetapec tests/run_tests.sh

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

# bison makes parser.tab.c and parser.tab.h (and parser.output for debugging conflicts)
parser.tab.c parser.tab.h: parser.y
	bison -d -v parser.y

# flex needs the token definitions from parser.tab.h
lex.yy.c: lexer.l parser.tab.h
	flex lexer.l

# runtime library for compiled programs: tape.c + rt.c
# rt_tape.o: tape.c tape.h
# 	$(CC) $(CFLAGS) -c tape.c -o $@

# rt.o: rt.c tape.h
# 	$(CC) $(CFLAGS) -c rt.c -o $@

rt_tape.o: tape.c tape.h
	$(CC) $(CFLAGS) -fPIC -c tape.c -o $@

rt.o: rt.c tape.h
	$(CC) $(CFLAGS) -fPIC -c rt.c -o $@

$(RTLIB): rt_tape.o rt.o
	ar rcs $@ rt_tape.o rt.o

# compare the interpreter with the compiler on every example
test: all
	sh tests/run_tests.sh

clean:
	rm -f $(TARGET) $(RTLIB) parser.tab.c parser.tab.h parser.output lex.yy.c *.o
	rm -rf tests/out

.PHONY: all test clean
