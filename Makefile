CC     = gcc
CFLAGS = -std=gnu17 -Wall -Wextra -g
TARGET = bytetape

SRCS   = parser.tab.c lex.yy.c ast.c interp.c tape.c main.c
HDRS   = ast.h interp.h tape.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

# bison makes parser.tab.c and parser.tab.h (and parser.output for debugging conflicts)
parser.tab.c parser.tab.h: parser.y
	bison -d -v parser.y

# flex needs the token definitions from parser.tab.h
lex.yy.c: lexer.l parser.tab.h
	flex lexer.l

clean:
	rm -f $(TARGET) parser.tab.c parser.tab.h parser.output lex.yy.c

.PHONY: all clean
