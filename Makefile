CC=gcc
CFLAGS=-Wall -O3 -std=c99 -fopenmp -march=native -flto
LDFLAGS=-Wl,-z,noexecstack
PREFIX=bin
SRCDIR=src
INCDIR=include
ASMDIR=$(SRCDIR)/asm

SRC=$(SRCDIR)/main.c $(SRCDIR)/solver.c $(SRCDIR)/lut.c $(SRCDIR)/solver_creative.c $(SRCDIR)/bench.c $(SRCDIR)/dup_avx2_gather.c $(SRCDIR)/dup_avx512.c

# ASM sources: FASM (unrolled scalar -af), NASM (AVX2 gather -an)
FASM_OBJ=$(ASMDIR)/dup_avx2_unrolled.o
NASM_OBJ=$(ASMDIR)/dup_avx2_gather_nasm.o

# Link both ASM implementations
OBJ=$(SRC:.c=.o) $(FASM_OBJ) $(NASM_OBJ)
TARGET=$(PREFIX)/golomb

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(PREFIX)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

# FASM rule (unrolled scalar)
$(ASMDIR)/dup_avx2_unrolled.o: $(ASMDIR)/dup_avx2_unrolled.asm
	fasm $< $@

# NASM rule for AVX2 gather (alternative, use with: make ASM_OBJ=src/dup_avx2_gather_nasm.o)
$(ASMDIR)/dup_avx2_gather_nasm.o: $(ASMDIR)/dup_avx2_gather.asm
	nasm -f elf64 -o $@ $<

clean:
	rm -rf $(OBJ) $(TARGET) $(ASMDIR)/*.o $(SRCDIR)/*.o

.PHONY: all clean
