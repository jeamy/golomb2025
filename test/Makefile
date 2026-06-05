CC ?= gcc
CFLAGS ?= -Wall -Wextra -O3 -std=c99 -fopenmp -march=native -flto
LDFLAGS ?= -lm

ROOT := ..
INCDIR := $(ROOT)/include
SRCDIR := $(ROOT)/src
BINDIR := bin

SOLVER_SRC := \
	$(SRCDIR)/solver.c \
	$(SRCDIR)/lut.c \
	$(SRCDIR)/solver_physics.c \
	$(SRCDIR)/solver_evolution.c \
	$(SRCDIR)/solver_traditional_opt.c

TESTS := \
	benchmark_algorithms_9_12 \
	benchmark_algorithms_13_14 \
	benchmark_solvers \
	benchmark_extended \
	benchmark_n10_11 \
	test_new_solvers

all: $(TESTS:%=$(BINDIR)/%)

$(BINDIR):
	mkdir -p $@

$(BINDIR)/%: %.c $(SOLVER_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) $< $(SOLVER_SRC) $(LDFLAGS) -o $@

clean:
	rm -rf $(BINDIR)

.PHONY: all clean
