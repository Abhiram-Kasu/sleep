.DEFAULT_GOAL := build
ARGS ?=
# Benchmark runs must not compete for CPU.
.NOTPARALLEL:

.PHONY: build native run aot run-aot bench test clean

native:
	cmake -S ../.. -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
	cmake --build build --target sleep_runtime_shared

build: native
	dotnet build Benchmark.cs -c Release

run: native
	dotnet run --file Benchmark.cs -c Release -- $(ARGS)

aot: native
	dotnet publish Benchmark.cs -c Release -o build/aot

run-aot: aot
	./build/aot/Benchmark $(ARGS)

# Run the two benchmark modes sequentially.
bench:
	$(MAKE) run
	$(MAKE) run-aot

test:
	$(MAKE) run ARGS="3 10 2"
	$(MAKE) run-aot ARGS="3 10 2"

clean:
	dotnet clean Benchmark.cs -c Release --verbosity quiet
	rm -rf build
