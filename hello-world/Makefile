CC := gcc
CFLAGS := -O2 -Wall -Wextra -std=gnu11

all: copier normal benchmarks/copy_bench

copier: copier-main.c copier.c copier.h def.h
	$(CC) $(CFLAGS) copier-main.c copier.c -o copier

normal: normal.c def.h
	$(CC) $(CFLAGS) normal.c -o normal

benchmarks/copy_bench: benchmarks/copy_bench.c copier.c copier.h def.h
	$(CC) $(CFLAGS) benchmarks/copy_bench.c copier.c -o benchmarks/copy_bench -lm

.PHONY: clean

clean:
	rm -f copier normal benchmarks/copy_bench