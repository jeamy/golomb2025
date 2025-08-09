# NVIDIA/CUDA build for golomb_nv

CUDA_HOME ?= /usr/local/cuda
# Compiler toolkit (headers, nvcc)
CUDA_TOOLKIT ?= $(CUDA_HOME)
# Runtime to link/load (libcudart), can differ from toolkit
CUDA_RUNTIME_HOME ?= $(CUDA_HOME)
NVCC ?= $(CUDA_TOOLKIT)/bin/nvcc
CC   ?= gcc
HOSTCXX ?= g++
CFLAGS := -O3 -Wall -std=c11 -I../include
NVFLAGS := -O3 -std=c++17 -Xcompiler "-fopenmp -O3 -Wall" -I../include -gencode arch=compute_75,code=sm_75 -ccbin $(HOSTCXX)
# Link OpenMP runtime explicitly instead of passing -fopenmp to nvcc
# (nvcc does not accept -fopenmp as a direct option at link time).
LDFLAGS := -lgomp -lpthread

BIN_DIR := ./
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
	$(NVCC) $(NVFLAGS) -o $@ $(OBJS_CUDA) $(OBJS_HOST) -L$(CUDA_RUNTIME_HOME)/lib64 -Xlinker -rpath -Xlinker $(CUDA_RUNTIME_HOME)/lib64 $(LDFLAGS)

clean:
	rm -f $(OBJS_CUDA) $(OBJS_HOST) $(BIN)

.PHONY: all clean
