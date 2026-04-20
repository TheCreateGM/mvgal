# MVGAL Benchmark Suite Makefile
# Multi-Vendor GPU Aggregation Layer for Linux

CC = gcc
CFLAGS = -O2 -Wall -Wextra -I.
LDFLAGS = -lpthread -lm

# Source files
BENCHMARKS_SRC = benchmarks.c
SYNTHETIC_SRC = synthetic/mvgal_synthetic_bench.c
REALWORLD_SRC = real_world/mvgal_realworld_bench.c
STRESS_SRC = stress/mvgal_stress_bench.c

# Object files
BENCHMARKS_OBJ = benchmarks.o
SYNTHETIC_OBJ = synthetic/mvgal_synthetic_bench.o
REALWORLD_OBJ = real_world/mvgal_realworld_bench.o
STRESS_OBJ = stress/mvgal_stress_bench.o

# Executables
SYNTHETIC_BIN = synthetic/mvgal_synthetic_bench
REALWORLD_BIN = real_world/mvgal_realworld_bench
STRESS_BIN = stress/mvgal_stress_bench

# Static library
LIB_BENCHMARKS = libbenchmarks.a

.PHONY: all clean run

all: $(LIB_BENCHMARKS) $(SYNTHETIC_BIN) $(REALWORLD_BIN) $(STRESS_BIN)

$(LIB_BENCHMARKS): $(BENCHMARKS_OBJ)
	ar rcs $@ $^

$(BENCHMARKS_OBJ): $(BENCHMARKS_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(SYNTHETIC_OBJ): $(SYNTHETIC_SRC) $(BENCHMARKS_OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

$(REALWORLD_OBJ): $(REALWORLD_SRC) $(BENCHMARKS_OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

$(STRESS_OBJ): $(STRESS_SRC) $(BENCHMARKS_OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

$(SYNTHETIC_BIN): $(SYNTHETIC_OBJ) $(BENCHMARKS_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(REALWORLD_BIN): $(REALWORLD_OBJ) $(BENCHMARKS_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(STRESS_BIN): $(STRESS_OBJ) $(BENCHMARKS_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(BENCHMARKS_OBJ) $(SYNTHETIC_OBJ) $(REALWORLD_OBJ) $(STRESS_OBJ)
	rm -f $(SYNTHETIC_BIN) $(REALWORLD_BIN) $(STRESS_BIN)
	rm -f $(LIB_BENCHMARKS)
	rm -rf results/*.txt

run: all
	@echo "Running MVGAL Synthetic Benchmarks..."
	./$(SYNTHETIC_BIN) -q
	@echo "Running MVGAL Real-World Benchmarks..."
	./$(REALWORLD_BIN) -q
	@echo "Running MVGAL Stress Benchmarks..."
	./$(STRESS_BIN) -q -d 1000

run-synthetic: $(SYNTHETIC_BIN)
	./$(SYNTHETIC_BIN) -q

run-realworld: $(REALWORLD_BIN)
	./$(REALWORLD_BIN) -q

run-stress: $(STRESS_BIN)
	./$(STRESS_BIN) -q -d 1000
