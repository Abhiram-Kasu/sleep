# C# thread-pool benchmark

Single-file .NET 10 app; requires the root project’s C++26 toolchain. From this folder:

```sh
cmake -S ../.. -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --target sleep_runtime_shared
dotnet run --file Benchmark.cs -c Release
```

Optional arguments: `-- [jobs] [numbersPerJob] [rounds]`.

## Results

Local macOS arm64 run, .NET 10.0.0, 11 reported logical CPUs; Release with tiered
compilation disabled. 44 jobs × 1,000,000 numbers, five measured rounds:

| Scheduler | Median time |
| --- | ---: |
| libsleep_runtime | 885.18 ms |
| Task.Run | 888.16 ms |

Essentially tied on this workload. Both run the same C# prime-counting function.
Each path gets a full warmup, measured order alternates, and every job is checked
against a sequential reference for its result and exactly one execution
(2,661,384 primes per run).

Times include submission and waiting, plus native pool creation/destruction;
Task.Run uses .NET’s warmed shared pool. No native pool remains alive during the
Task.Run measurement. These are local measurements, not a general speed claim.
