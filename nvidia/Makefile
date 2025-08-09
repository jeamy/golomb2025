# NVIDIA/CUDA build for golomb_nv

NVCC ?= nvcc
CC   ?= gcc
CFLAGS := -O3 -Wall -std=c11 -I../include
NVFLAGS := -O3 -std=c++17 -Xcompiler "-fopenmp -O3 -Wall" -I../include
LDFLAGS := -fopenmp -lpthread

BIN_DIR := ../bin
BIN := $(BIN_DIR)/golomb_nv

OBJS_HOST := ../src/lut.o
OBJS_CUDA := golomb_nv.o

all: $(BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

../src/lut.o: ../src/lut.c ../include/golomb.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cu ../include/golomb.h
	$(NVCC) $(NVFLAGS) -c -o $@ $<

$(BIN): $(BIN_DIR) $(OBJS_HOST) $(OBJS_CUDA)
	$(NVCC) $(NVFLAGS) -o $@ $(OBJS_CUDA) $(OBJS_HOST) $(LDFLAGS)

clean:
	rm -f $(OBJS_CUDA) $(OBJS_HOST) $(BIN)

.PHONY: all clean
