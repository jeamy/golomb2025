CC=gcc
CFLAGS=-Wall -O3 -std=c99 -fopenmp -march=native -flto
PREFIX=bin
SRCDIR=src
INCDIR=include

SRC=$(SRCDIR)/main.c $(SRCDIR)/solver.c $(SRCDIR)/lut.c $(SRCDIR)/solver_creative.c $(SRCDIR)/bench.c
ASM_SRC=$(SRCDIR)/dup_avx2.asm
OBJ=$(SRC:.c=.o) $(ASM_SRC:.asm=.o)
TARGET=$(PREFIX)/golomb

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(PREFIX)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

%.o: %.asm
	fasm $< $@

clean:
	rm -rf $(OBJ) $(TARGET)

.PHONY: all clean
