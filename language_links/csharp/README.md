# Thread-pool benchmarks

.NET 10, BenchmarkDotNet 0.15.8, and the root CMake/C++26 toolchain are required.
One C# source file plus a small project lets BenchmarkDotNet generate isolated
benchmark executables. Native calls use `LibraryImport` and an
`UnmanagedCallersOnly` callback.

From this directory:

```sh
make test                 # C++ pool tests + C# scheduler stress checks
make run                  # BenchmarkDotNet, ReadyToRun + tiered JIT
make run-aot              # BenchmarkDotNet, Native AOT (also: make aot)
make bench                # Both runtimes
make smoke                # All cases/runtimes once; validates wiring, not speed
make bench ARGS='--job short'  # Shorter statistical run
make clean
```

`make` builds the native library and benchmark host. The launcher runs on the
JIT; managed benchmark children are published as ReadyToRun for the current
host RID. The build checks the native headers in both the generated harness and
`Benchmark.dll`, and explicitly enables ReadyToRun in the child environment.
`make run-aot` builds separate Native AOT children. Both runtimes load the shared
library from `build/` beside the source.

[ReadyToRun](https://learn.microsoft.com/en-us/dotnet/core/deploying/ready-to-run) retains IL and JIT fallback. Tiered compilation stays enabled and may
replace hot R2R methods with optimized JIT code during warmup; these are steady
state pool benchmarks, not a measurement of ReadyToRun startup benefits.
Reports (Markdown, CSV, HTML, and logs) go to `build/BenchmarkDotNet/`.

Pass BenchmarkDotNet options with `ARGS`; use `FILTER` to select benchmark
names or parameter values. Direct launcher invocations select Native AOT with
`--aot`, both builds with `--both`, or ReadyToRun by default (`--runtimes` is not
used, since it would replace the custom publish toolchain):

```sh
make run FILTER='*UniformCpu*' ARGS='--job short'
make bench FILTER='*PoolThroughput*' ARGS='--job short'
```

## What is measured

`PoolThroughput` compares `Task.Run`, a persistent native pool, and sequential
execution, using 64 and 1,024 jobs per batch:

| Workload | Purpose |
| --- | --- |
| Scheduling | Zero CPU iterations; exposes submission, dispatch, and completion overhead |
| UniformCpu | 16,777,216 integer-mixing iterations divided evenly across jobs |
| SkewedCpu | Same total work; the final 1/8 of jobs carry 7/8 of it, exposing a long tail |

Pool creation, input/delegate creation, stable-handle registration, and expected
results are outside throughput timing. Each invocation submits and waits for an
entire batch. Native completion uses a countdown; .NET uses `Task.WaitAll` and
allocates its normal Task objects. Both execute the same CPU function, store a
result, and increment an execution counter. Sequential execution shows the cost
of the underlying work and bookkeeping without scheduling.

`PoolLifecycle.NativeCreateAndDestroy` separately measures native startup and
shutdown. There is no corresponding .NET create/destroy API. Both pools retain
their default scheduling policies; this is not a fixed-worker-count comparison.
Native workers exist only in their own benchmark case, never alongside the
Task.Run or sequential baseline.

BenchmarkDotNet controls warmup, calibration, iterations, and process isolation.
It reports time, error, standard deviation, ratios to Task.Run, managed
allocations, and .NET threading counters. Memory figures exclude C++ allocations;
threading counters do not count native queue locks or native worker completions.
One reported operation is one complete batch, not one task.

## Correctness

`make test` checks serial-reference results and exactly-once execution after
each batch, repeated use, four simultaneous producers, recovery after 500 ms
idle, repeated single-job handoff, and empty-pool destruction. It also runs the
existing C++ tests for resizing and shutdown. Benchmark cleanup checks every
job's cumulative execution count and final result outside timed measurements.
Waits have a 30-second deadline; failures return a nonzero exit code.

## Results

Apple M3 Pro (11 cores reported), macOS 27, .NET 10.0.0 / SDK 10.0.100.
`make bench ARGS='--job short'`: one launch, three warmups, three measured
iterations per case. All 38 cases passed; published R2R headers were verified.
Mean milliseconds per batch:

| Build | Jobs | Workload | Task.Run | Native pool |
| --- | ---: | --- | ---: | ---: |
| ReadyToRun | 64 | Scheduling | 0.019 | 0.040 |
| ReadyToRun | 64 | UniformCpu | 3.512 | 3.432 |
| ReadyToRun | 64 | SkewedCpu | 4.465 | 4.422 |
| ReadyToRun | 1024 | Scheduling | 0.209 | 0.559 |
| ReadyToRun | 1024 | UniformCpu | 3.254 | 4.208 |
| ReadyToRun | 1024 | SkewedCpu | 3.928 | 4.131 |
| NativeAOT | 64 | Scheduling | 0.021 | 0.055 |
| NativeAOT | 64 | UniformCpu | 3.377 | 5.084 |
| NativeAOT | 64 | SkewedCpu | 4.419 | 4.690 |
| NativeAOT | 1024 | Scheduling | 0.174 | 0.629 |
| NativeAOT | 1024 | UniformCpu | 3.225 | 3.619 |
| NativeAOT | 1024 | SkewedCpu | 3.294 | 3.879 |

Native pool creation/destruction averaged 0.108 ms under ReadyToRun and 0.106 ms
under Native AOT. [Full report](RESULTS.md) includes the sequential baseline,
error estimates, standard deviations, allocations, and threading counters.
Short-run variation is substantial in some cases; use `make bench` for longer
measurements. Earlier stopwatch/prime-counting results used a different workload
and are not comparable to these results.
