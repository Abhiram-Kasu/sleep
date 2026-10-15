# BenchmarkDotNet short-run results

Measured on 2026-10-14 with `make bench ARGS='--job short'`.
All 38 cases passed. Each case uses one launch, three warmup iterations, and
three measured iterations. These short-run estimates are not definitive rankings.
Managed benchmark assemblies were published and verified as ReadyToRun; tiered
JIT remains enabled. Allocations and threading counters cover managed activity
only. A throughput operation is one full batch, not one task.

## Throughput

```

BenchmarkDotNet v0.15.8, macOS 27.0 (26A428) [Darwin 27.0.0]
Apple M3 Pro, 1 CPU, 11 logical and 11 physical cores
.NET SDK 10.0.100
  [Host]     : .NET 10.0.0 (10.0.0, 10.0.25.52411), Arm64 RyuJIT armv8.0-a
  NativeAOT  : .NET 10.0.0, Arm64 NativeAOT armv8.0-a
  ReadyToRun : .NET 10.0.0 (10.0.0, 10.0.25.52411), Arm64 RyuJIT armv8.0-a

IterationCount=3  LaunchCount=1  WarmupCount=3  

```
| Method     | Job        | EnvironmentVariables                     | Runtime        | JobCount | Workload   | Mean             | Error             | StdDev           | Ratio | RatioSD | Gen0   | Completed Work Items | Lock Contentions | Gen1   | Allocated | Alloc Ratio |
|----------- |----------- |----------------------------------------- |--------------- |--------- |----------- |-----------------:|------------------:|-----------------:|------:|--------:|-------:|---------------------:|-----------------:|-------:|----------:|------------:|
| **TaskRun**    | **NativeAOT**  | **Empty**                                    | **NativeAOT 10.0** | **64**       | **Scheduling** |     **20,575.90 ns** |      **6,887.388 ns** |       **377.521 ns** | **1.000** |    **0.02** | **0.5493** |              **64.0000** |                **-** |      **-** |    **4694 B** |        **1.00** |
| NativePool | NativeAOT  | Empty                                    | NativeAOT 10.0 | 64       | Scheduling |     54,501.75 ns |      9,590.901 ns |       525.710 ns | 2.649 |    0.05 |      - |                    - |                - |      - |         - |        0.00 |
| Sequential | NativeAOT  | Empty                                    | NativeAOT 10.0 | 64       | Scheduling |         70.89 ns |          0.334 ns |         0.018 ns | 0.003 |    0.00 |      - |                    - |                - |      - |         - |        0.00 |
|            |            |                                          |                |          |            |                  |                   |                  |       |         |        |                      |                  |        |           |             |
| **TaskRun**    | **NativeAOT**  | **Empty**                                    | **NativeAOT 10.0** | **64**       | **UniformCpu** |  **3,377,238.82 ns** |    **200,193.304 ns** |    **10,973.271 ns** |  **1.00** |    **0.00** |      **-** |              **64.0000** |                **-** |      **-** |    **4854 B** |       **1.000** |
| NativePool | NativeAOT  | Empty                                    | NativeAOT 10.0 | 64       | UniformCpu |  5,084,474.28 ns |  5,628,975.355 ns |   308,543.135 ns |  1.51 |    0.08 |      - |                    - |                - |      - |       3 B |       0.001 |
| Sequential | NativeAOT  | Empty                                    | NativeAOT 10.0 | 64       | UniformCpu | 25,988,934.46 ns | 17,626,594.605 ns |   966,173.132 ns |  7.70 |    0.25 |      - |                    - |                - |      - |      21 B |       0.004 |
|            |            |                                          |                |          |            |                  |                   |                  |       |         |        |                      |                  |        |           |             |
| **TaskRun**    | **NativeAOT**  | **Empty**                                    | **NativeAOT 10.0** | **64**       | **SkewedCpu**  |  **4,419,063.58 ns** |    **408,516.424 ns** |    **22,392.164 ns** |  **1.00** |    **0.01** |      **-** |              **64.0000** |                **-** |      **-** |    **4860 B** |       **1.000** |
| NativePool | NativeAOT  | Empty                                    | NativeAOT 10.0 | 64       | SkewedCpu  |  4,689,839.85 ns |  5,794,266.572 ns |   317,603.304 ns |  1.06 |    0.06 |      - |                    - |                - |      - |       5 B |       0.001 |
| Sequential | NativeAOT  | Empty                                    | NativeAOT 10.0 | 64       | SkewedCpu  | 25,405,759.55 ns | 18,032,489.254 ns |   988,421.588 ns |  5.75 |    0.20 |      - |                    - |                - |      - |      21 B |       0.004 |
|            |            |                                          |                |          |            |                  |                   |                  |       |         |        |                      |                  |        |           |             |
| **TaskRun**    | **NativeAOT**  | **Empty**                                    | **NativeAOT 10.0** | **1024**     | **Scheduling** |    **173,586.03 ns** |    **150,043.248 ns** |     **8,224.377 ns** | **1.002** |    **0.06** | **8.7891** |            **1024.0000** |                **-** | **1.7090** |   **73820 B** |       **1.000** |
| NativePool | NativeAOT  | Empty                                    | NativeAOT 10.0 | 1024     | Scheduling |    628,991.67 ns |    128,765.271 ns |     7,058.059 ns | 3.629 |    0.16 |      - |                    - |                - |      - |       1 B |       0.000 |
| Sequential | NativeAOT  | Empty                                    | NativeAOT 10.0 | 1024     | Scheduling |      1,093.06 ns |         31.573 ns |         1.731 ns | 0.006 |    0.00 |      - |                    - |                - |      - |         - |       0.000 |
|            |            |                                          |                |          |            |                  |                   |                  |       |         |        |                      |                  |        |           |             |
| **TaskRun**    | **NativeAOT**  | **Empty**                                    | **NativeAOT 10.0** | **1024**     | **UniformCpu** |  **3,225,356.12 ns** |    **476,758.345 ns** |    **26,132.734 ns** |  **1.00** |    **0.01** | **7.8125** |            **1024.0000** |                **-** |      **-** |   **73915 B** |       **1.000** |
| NativePool | NativeAOT  | Empty                                    | NativeAOT 10.0 | 1024     | UniformCpu |  3,619,214.47 ns |    685,287.811 ns |    37,562.937 ns |  1.12 |    0.01 |      - |                    - |                - |      - |       3 B |       0.000 |
| Sequential | NativeAOT  | Empty                                    | NativeAOT 10.0 | 1024     | UniformCpu | 27,036,339.85 ns | 42,086,832.680 ns | 2,306,921.324 ns |  8.38 |    0.62 |      - |                    - |                - |      - |      12 B |       0.000 |
|            |            |                                          |                |          |            |                  |                   |                  |       |         |        |                      |                  |        |           |             |
| **TaskRun**    | **NativeAOT**  | **Empty**                                    | **NativeAOT 10.0** | **1024**     | **SkewedCpu**  |  **3,293,964.41 ns** |    **203,051.286 ns** |    **11,129.926 ns** |  **1.00** |    **0.00** | **7.8125** |            **1024.0000** |                **-** |      **-** |   **73985 B** |       **1.000** |
| NativePool | NativeAOT  | Empty                                    | NativeAOT 10.0 | 1024     | SkewedCpu  |  3,878,594.73 ns |  1,043,860.528 ns |    57,217.518 ns |  1.18 |    0.02 |      - |                    - |                - |      - |       3 B |       0.000 |
| Sequential | NativeAOT  | Empty                                    | NativeAOT 10.0 | 1024     | SkewedCpu  | 25,409,173.61 ns | 19,278,846.055 ns | 1,056,738.610 ns |  7.71 |    0.28 |      - |                    - |                - |      - |      21 B |       0.000 |
|            |            |                                          |                |          |            |                  |                   |                  |       |         |        |                      |                  |        |           |             |
| **TaskRun**    | **ReadyToRun** | **DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1** | **.NET 10.0**      | **64**       | **Scheduling** |     **18,868.25 ns** |     **18,209.061 ns** |       **998.100 ns** | **1.002** |    **0.06** | **0.5493** |              **64.0000** |                **-** |      **-** |    **4699 B** |        **1.00** |
| NativePool | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 64       | Scheduling |     39,735.41 ns |     84,995.510 ns |     4,658.891 ns | 2.110 |    0.23 |      - |                    - |                - |      - |         - |        0.00 |
| Sequential | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 64       | Scheduling |         71.35 ns |         27.230 ns |         1.493 ns | 0.004 |    0.00 |      - |                    - |                - |      - |         - |        0.00 |
|            |            |                                          |                |          |            |                  |                   |                  |       |         |        |                      |                  |        |           |             |
| **TaskRun**    | **ReadyToRun** | **DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1** | **.NET 10.0**      | **64**       | **UniformCpu** |  **3,512,235.35 ns** |    **218,558.998 ns** |    **11,979.956 ns** |  **1.00** |    **0.00** |      **-** |              **64.0000** |                **-** |      **-** |    **4727 B** |        **1.00** |
| NativePool | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 64       | UniformCpu |  3,431,907.06 ns |    207,048.853 ns |    11,349.046 ns |  0.98 |    0.00 |      - |                    - |                - |      - |         - |        0.00 |
| Sequential | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 64       | UniformCpu | 24,951,960.93 ns |  2,171,252.412 ns |   119,013.672 ns |  7.10 |    0.04 |      - |                    - |                - |      - |         - |        0.00 |
|            |            |                                          |                |          |            |                  |                   |                  |       |         |        |                      |                  |        |           |             |
| **TaskRun**    | **ReadyToRun** | **DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1** | **.NET 10.0**      | **64**       | **SkewedCpu**  |  **4,465,452.37 ns** |  **1,351,044.394 ns** |    **74,055.302 ns** |  **1.00** |    **0.02** |      **-** |              **64.0000** |                **-** |      **-** |    **4728 B** |        **1.00** |
| NativePool | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 64       | SkewedCpu  |  4,421,842.88 ns |  1,015,712.667 ns |    55,674.639 ns |  0.99 |    0.02 |      - |                    - |                - |      - |         - |        0.00 |
| Sequential | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 64       | SkewedCpu  | 24,868,558.59 ns |    661,597.963 ns |    36,264.417 ns |  5.57 |    0.08 |      - |                    - |                - |      - |         - |        0.00 |
|            |            |                                          |                |          |            |                  |                   |                  |       |         |        |                      |                  |        |           |             |
| **TaskRun**    | **ReadyToRun** | **DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1** | **.NET 10.0**      | **1024**     | **Scheduling** |    **208,982.28 ns** |    **118,670.660 ns** |     **6,504.739 ns** | **1.001** |    **0.04** | **8.7891** |            **1024.0000** |                **-** | **1.7090** |   **73822 B** |        **1.00** |
| NativePool | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 1024     | Scheduling |    559,183.91 ns |    896,746.518 ns |    49,153.703 ns | 2.677 |    0.22 |      - |                    - |                - |      - |         - |        0.00 |
| Sequential | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 1024     | Scheduling |      1,093.63 ns |         32.721 ns |         1.794 ns | 0.005 |    0.00 |      - |                    - |                - |      - |         - |        0.00 |
|            |            |                                          |                |          |            |                  |                   |                  |       |         |        |                      |                  |        |           |             |
| **TaskRun**    | **ReadyToRun** | **DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1** | **.NET 10.0**      | **1024**     | **UniformCpu** |  **3,254,093.26 ns** |    **220,435.606 ns** |    **12,082.819 ns** |  **1.00** |    **0.00** | **7.8125** |            **1024.0000** |                **-** |      **-** |   **73835 B** |        **1.00** |
| NativePool | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 1024     | UniformCpu |  4,208,072.05 ns |  4,786,545.435 ns |   262,366.708 ns |  1.29 |    0.07 |      - |                    - |                - |      - |         - |        0.00 |
| Sequential | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 1024     | UniformCpu | 26,035,741.75 ns | 19,602,309.712 ns | 1,074,468.745 ns |  8.00 |    0.29 |      - |                    - |                - |      - |         - |        0.00 |
|            |            |                                          |                |          |            |                  |                   |                  |       |         |        |                      |                  |        |           |             |
| **TaskRun**    | **ReadyToRun** | **DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1** | **.NET 10.0**      | **1024**     | **SkewedCpu**  |  **3,927,823.41 ns** |  **5,218,057.296 ns** |   **286,019.329 ns** |  **1.00** |    **0.09** | **7.8125** |            **1024.0000** |                **-** |      **-** |   **73844 B** |        **1.00** |
| NativePool | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 1024     | SkewedCpu  |  4,130,640.30 ns |  3,112,102.809 ns |   170,584.857 ns |  1.06 |    0.08 |      - |                    - |                - |      - |         - |        0.00 |
| Sequential | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 1024     | SkewedCpu  | 25,158,596.35 ns |  2,056,930.874 ns |   112,747.322 ns |  6.43 |    0.42 |      - |                    - |                - |      - |         - |        0.00 |

## Native pool lifecycle

```

BenchmarkDotNet v0.15.8, macOS 27.0 (26A428) [Darwin 27.0.0]
Apple M3 Pro, 1 CPU, 11 logical and 11 physical cores
.NET SDK 10.0.100
  [Host]     : .NET 10.0.0 (10.0.0, 10.0.25.52411), Arm64 RyuJIT armv8.0-a
  NativeAOT  : .NET 10.0.0, Arm64 NativeAOT armv8.0-a
  ReadyToRun : .NET 10.0.0 (10.0.0, 10.0.25.52411), Arm64 RyuJIT armv8.0-a

IterationCount=3  LaunchCount=1  WarmupCount=3  

```
| Method                 | Job        | EnvironmentVariables                     | Runtime        | Mean     | Error    | StdDev  |
|----------------------- |----------- |----------------------------------------- |--------------- |---------:|---------:|--------:|
| NativeCreateAndDestroy | NativeAOT  | Empty                                    | NativeAOT 10.0 | 105.5 μs | 40.79 μs | 2.24 μs |
| NativeCreateAndDestroy | ReadyToRun | DOTNET_ReadyToRun=1,COMPlus_ReadyToRun=1 | .NET 10.0      | 107.5 μs | 15.29 μs | 0.84 μs |
