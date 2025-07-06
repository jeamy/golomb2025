CC=gcc
CFLAGS=-Wall -O3 -std=c99 -fopenmp -march=native -flto
LDFLAGS=-Wl,-z,noexecstack
PREFIX=bin
SRCDIR=src
INCDIR=include

SRC=$(SRCDIR)/main.c $(SRCDIR)/solver.c $(SRCDIR)/lut.c $(SRCDIR)/solver_creative.c $(SRCDIR)/bench.c $(SRCDIR)/dup_avx2_gather.c $(SRCDIR)/dup_avx512.c
ASM_SRC=$(SRCDIR)/dup_avx2_unrolled.asm
OBJ=$(SRC:.c=.o) $(ASM_SRC:.asm=.o)
TARGET=$(PREFIX)/golomb

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(PREFIX)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

%.o: %.asm
	fasm $< $@

clean:
	rm -rf $(OBJ) $(TARGET)

.PHONY: all clean
