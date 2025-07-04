CC=gcc
CFLAGS=-Wall -O3 -std=c99 -fopenmp -march=native -flto
PREFIX=bin
SRCDIR=src
INCDIR=include

SRC=$(SRCDIR)/main.c $(SRCDIR)/solver.c $(SRCDIR)/lut.c $(SRCDIR)/solver_creative.c
OBJ=$(SRC:.c=.o)
TARGET=$(PREFIX)/golomb

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(PREFIX)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

clean:
	rm -rf $(OBJ) $(TARGET)

.PHONY: all clean
