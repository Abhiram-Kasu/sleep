# C# thread-pool benchmark

Single-file .NET 10 app using `LibraryImport` and an `UnmanagedCallersOnly`
callback. Requires `make`, .NET 10, and the root project’s CMake/C++26 toolchain.
From this folder:

```sh
make          # Build the shared library and C# app in Release
make run      # Run the JIT benchmark
make aot      # Publish Native AOT for the current host
make run-aot  # Publish and run the Native AOT benchmark
make bench    # Run JIT, then Native AOT (sequentially)
make test     # Quick correctness checks under both builds
make clean    # Clean the C# build and remove local build outputs
```

Override `[jobs] [numbersPerJob] [rounds]` with, for example:

```sh
make bench ARGS="44 1000000 5"
```

`run`, `aot`, and `run-aot` build the shared library automatically. The AOT
executable is `build/aot/Benchmark`; it loads the shared runtime from `build/`
next to the source file. `make test` uses three jobs × ten numbers, two rounds.

## Results

Local macOS arm64 runs, .NET 10.0.0 / SDK 10.0.100, 11 reported logical CPUs;
Release, JIT tiered compilation disabled. Each run uses 44 jobs × 1,000,000
numbers and five measured rounds. Times below are medians:

| Build / imports | libsleep_runtime | Task.Run |
| --- | ---: | ---: |
| JIT / DllImport (original baseline) | 885.18 ms | 888.16 ms |
| JIT / LibraryImport | 864.83 ms | 853.80 ms |
| Native AOT / LibraryImport | 934.15 ms | 858.94 ms |

Both schedulers run the same C# prime-counting function. Each path gets a full
warmup, measured order alternates, and every job is checked against a sequential
reference for its result and exactly one execution (2,661,384 primes per run).
All runs passed, including the published arm64 Native AOT executable.

Times include submission and waiting, plus native pool creation/destruction;
Task.Run uses .NET’s warmed shared pool. No native pool remains alive during
Task.Run measurements. These are separate local runs with normal timing
variation; they do not isolate the effect of changing imports or compilation.
