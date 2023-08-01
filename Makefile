PWD := $(CURDIR)

INC=$(PWD)/include
INC_PARAMS=$(INC:%=-I%)

CC ?= gcc
CFLAGS:=-g 
CFLAGS+=-std=c11
CFLAGS+=-Wall
CFLAGS+=-O1
CFLAGS+=-rdynamic

DEBUG_FLAGS=
ifneq ($(strip $(verbose)),)
#DEBUG_FLAGS+=-fsanitize=thread
DEBUG_FLAGS+=-D'CONFIG_DEBUG'
CFLAGS+=$(DEBUG_FLAGS)
endif

SRC:=src/osc.c
SRC+=src/parser.c
SRC+=src/token.c
SRC+=src/check_ownership.c
#SRC+=src/object_type.c

OBJ:=$(SRC:.c=.o)

BIN:=osc

ifeq ($(quiet),1)
OSC_CC=@$(CC)
OSC_RM=@rm
OSC_MKDIR=@mkdir
OSC_CP=@cp
OSC_MV=@mv
else
OSC_CC=$(CC)
OSC_RM=rm
OSC_MKDIR=mkdir
OSC_CP=cp
OSC_MV=mv
endif

%.o: %.c
	$(OSC_CC) $(CFLAGS) $(INC_PARAMS) -c $< -o $@

build: clean $(OBJ)
	$(OSC_CC) $(CFLAGS) $(OBJ) -o $(BIN)

clean:
	$(OSC_RM) -f src/*.o
	$(OSC_RM) -f $(BIN)

cscope:
	find $(PWD) -name "*.c" -o -name "*.h" > $(PWD)/cscope.files
	cscope -b -q

indent:
	clang-format -i include/*/*.[ch]
	clang-format -i src/*.[ch]

ifeq ($(quiet), 1)
.SILENT:
endif


