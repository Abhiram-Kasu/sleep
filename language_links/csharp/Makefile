.DEFAULT_GOAL := build
ARGS ?=
FILTER ?= *
# Benchmark processes must not compete for CPU.
.NOTPARALLEL:

.PHONY: build native run bench run-aot aot test smoke clean

native:
	cmake -S ../.. -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
	cmake --build build --target sleep_runtime_shared

build: native
	dotnet build Benchmark.csproj -c Release

# BenchmarkDotNet publishes isolated ReadyToRun or Native AOT executables.
run: native
	dotnet run --project Benchmark.csproj -c Release -- --filter '$(FILTER)' $(ARGS)

run-aot aot: native
	dotnet run --project Benchmark.csproj -c Release -- --filter '$(FILTER)' --aot $(ARGS)

bench: native
	dotnet run --project Benchmark.csproj -c Release -- --filter '$(FILTER)' --both $(ARGS)

test: native
	cmake --build build --target thread_pool_test sleep
	ctest --test-dir build --output-on-failure
	dotnet run --project Benchmark.csproj -c Release -- --test

# Validate every benchmark case in both runtimes; Dry timings are not results.
smoke: native
	dotnet run --project Benchmark.csproj -c Release -- --filter '$(FILTER)' --both --job Dry $(ARGS)

clean:
	rm -rf build bin obj BenchmarkDotNet.Artifacts
