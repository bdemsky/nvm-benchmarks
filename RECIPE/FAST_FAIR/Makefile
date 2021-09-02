.PHONY: all clean
.DEFAULT_GOAL := all
CXX=/scratch/nvm/benchmarks/g++
#CXX=g++
LIBS=-lpthread
INCLUDES=-I./include
CFLAGS=-O0 -std=c++11 -g -DCLFLUSH_OPT=1
#CFLAGS=-O3 -std=c++11 -g -S -DCLFLUSH_OPT=1
#CFLAGS=-O0 -std=c++11 -g -DCLWB=1
#CXX=/scratch/nvm/llvm-project/build/bin/clang++
output = example

all: main

main: example.cpp
	$(CXX) $(CFLAGS) -o $(output) example.cpp $(LIBS)

clean:
	rm $(output)
