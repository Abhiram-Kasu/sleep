using System;
using System.IO;
using System.Linq;
using System.Reflection.PortableExecutable;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using BenchmarkDotNet.Attributes;
using BenchmarkDotNet.Configs;
using BenchmarkDotNet.ConsoleArguments;
using BenchmarkDotNet.Environments;
using BenchmarkDotNet.Jobs;
using BenchmarkDotNet.Loggers;
using BenchmarkDotNet.Toolchains;
using BenchmarkDotNet.Toolchains.CsProj;
using BenchmarkDotNet.Toolchains.DotNetCli;
using BenchmarkDotNet.Toolchains.Results;
using BenchmarkDotNet.Running;

public enum WorkloadKind { Scheduling, UniformCpu, SkewedCpu }

internal static class Program
{
    internal static string SourceDirectory([CallerFilePath] string file = "") => Path.GetDirectoryName(file)!;

    public static int Main(string[] args)
    {
        if (args.SequenceEqual(new[] { "--test" }))
        {
            CorrectnessTests.Run();
            return 0;
        }
        bool aot = args.Contains("--aot"), both = args.Contains("--both");
        args = args.Where(arg => arg != "--aot" && arg != "--both").ToArray();
        if (args.Length == 0) args = new[] { "--filter", "*" };
        // Parse the run settings first, so --job Dry/Short and iteration options
        // apply to both explicit jobs without replacing the R2R toolchain.
        var parsed = ConfigParser.Parse(args, ConsoleLogger.Default);
        if (!parsed.isSuccess) return args.Any(arg => arg is "--help" or "--version") ? 0 : 1;
        if (parsed.options.Runtimes.Any())
            throw new ArgumentException("Use --aot or --both; managed cases always use ReadyToRun.");
        Job template = (parsed.config.GetJobs().SingleOrDefault() ?? Job.Default).UnfreezeCopy();
        template.Meta.IsMutator = false;
        var config = DefaultConfig.Instance
            .WithArtifactsPath(Path.Combine(SourceDirectory(), "build", "BenchmarkDotNet"))
            .AddJob(template.AsDefault());
        if (!aot || both)
            config.AddJob(template.AsDefault(false)
                .WithRuntime(CoreRuntime.Core10_0).WithToolchain(ReadyToRunToolchain.Instance)
                .WithEnvironmentVariable("DOTNET_ReadyToRun", "1")
                .WithEnvironmentVariable("COMPlus_ReadyToRun", "1").WithId("ReadyToRun"));
        if (aot || both)
            config.AddJob(template.AsDefault(false)
                .WithRuntime(NativeAotRuntime.Net10_0).WithId("NativeAOT"));
        var summaries = BenchmarkSwitcher.FromAssembly(typeof(Program).Assembly).Run(args, config);
        return summaries.Any(s => s.HasCriticalValidationErrors || s.Reports.Any(r => !r.Success)) ? 1 : 0;
    }
}

// The standard .NET toolchain only builds IL. Publish the generated harness
// AND its referenced benchmark assembly as R2R before BenchmarkDotNet executes it.
internal static class ReadyToRunToolchain
{
    internal static readonly IToolchain Instance = new Toolchain("ReadyToRun",
        new CsProjGenerator("net10.0", null!, null!, null!),
        new Publisher(), new DotNetCliExecutor(null!));

    private sealed class Publisher : IBuilder
    {
        public BuildResult Build(GenerateResult generated, BuildPartition partition, ILogger logger)
        {
            var result = new DotNetCliCommand(null!,
                $"/p:RuntimeIdentifier={RuntimeInformation.RuntimeIdentifier} /p:SelfContained=false /p:PublishReadyToRun=true",
                generated, logger, partition, Array.Empty<EnvironmentVariable>(), partition.Timeout)
                .RestoreThenBuildThenPublish();
            if (!result.IsBuildSuccess) return result;
            try
            {
                Verify(generated.ArtifactsPaths.ExecutablePath);
                Verify(Path.Combine(generated.ArtifactsPaths.BinariesDirectoryPath, "Benchmark.dll"));
                logger.WriteLineInfo("// Verified ReadyToRun headers: benchmark harness and Benchmark.dll");
                return result;
            }
            catch (Exception error) { return BuildResult.Failure(generated, error); }
        }

        private static void Verify(string path)
        {
            using var file = File.OpenRead(path);
            using var pe = new PEReader(file);
            var native = pe.PEHeaders.CorHeader?.ManagedNativeHeaderDirectory ?? default;
            if (native.Size == 0 || pe.GetSectionData(native.RelativeVirtualAddress).GetReader().ReadUInt32() != 0x00525452)
                throw new InvalidOperationException($"ReadyToRun RTR signature missing: {path}");
        }
    }
}

// Each benchmark case runs in its own BenchmarkDotNet child process. A native
// pool is created only for NativePool, so it cannot consume the baseline's CPU.
[MemoryDiagnoser]
[ThreadingDiagnoser]
[GroupBenchmarksBy(BenchmarkLogicalGroupRule.ByJob, BenchmarkLogicalGroupRule.ByParams)]
public class PoolThroughput
{
    [Params(64, 1024)] public int JobCount { get; set; }
    [Params(WorkloadKind.Scheduling, WorkloadKind.UniformCpu, WorkloadKind.SkewedCpu)]
    public WorkloadKind Workload { get; set; }

    private Batch _batch = null!;

    [GlobalSetup(Target = nameof(TaskRun))]
    public void SetupTasks()
    {
        _batch = new Batch(JobCount, Workload, native: false);
        _batch.RunTasks();
    }

    [GlobalSetup(Target = nameof(NativePool))]
    public void SetupNative()
    {
        _batch = new Batch(JobCount, Workload, native: true);
        _batch.RunNative();
    }

    [GlobalSetup(Target = nameof(Sequential))]
    public void SetupSequential() => _batch = new Batch(JobCount, Workload, native: false);

    [Benchmark(Baseline = true)] public void TaskRun() => _batch.RunTasks();
    [Benchmark] public void NativePool() => _batch.RunNative();
    [Benchmark] public void Sequential() => _batch.RunSequential();

    // Correctness checking is outside the timed path. The counters include
    // setup, calibration, warmup, and measured calls, not just measured calls.
    [GlobalCleanup] public void Cleanup() => _batch.Dispose();
}

// .NET's shared pool has no matching create/destroy API. Report native lifecycle
// cost separately instead of charging it only to native throughput measurements.
public class PoolLifecycle
{
    [GlobalSetup] public void LoadLibrary() => Native.EnsureLoaded();

    [Benchmark]
    public void NativeCreateAndDestroy()
    {
        nint pool = Native.CreatePool();
        Native.clean_thread_pool(pool);
    }
}

internal sealed class WorkItem
{
    internal readonly int Id, Iterations;
    internal readonly ulong Expected;
    internal readonly Action Action;
    internal readonly CountdownEvent Completion;
    internal ulong Result;
    internal long Calls;
    internal Exception? Error;

    internal WorkItem(int id, int iterations, CountdownEvent completion)
    {
        Id = id;
        Iterations = iterations;
        Completion = completion;
        Expected = CpuWork(id, iterations);
        Action = Execute;
    }

    internal void Execute()
    {
        Result = CpuWork(Id, Iterations);
        Interlocked.Increment(ref Calls);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    internal static ulong CpuWork(int id, int iterations)
    {
        ulong value = (ulong)id + 1;
        for (int i = 0; i < iterations; i++)
        {
            value ^= value << 13;
            value ^= value >> 7;
            value ^= value << 17;
        }
        return value;
    }
}

internal sealed class Batch : IDisposable
{
    // Fixed total CPU work: changing job count changes granularity, not work.
    internal const int TotalIterations = 16 * 1024 * 1024;
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(30);
    private readonly WorkItem[] _items;
    private readonly Task[] _tasks;
    private readonly GCHandle[] _handles;
    private readonly CountdownEvent _completion;
    private nint _pool;
    private long _batches;

    internal Batch(int count, WorkloadKind workload, bool native)
    {
        _completion = new CountdownEvent(0);
        _items = new WorkItem[count];
        _tasks = new Task[count];
        _handles = new GCHandle[native ? count : 0];
        for (int i = 0; i < count; i++)
            _items[i] = new WorkItem(i, IterationsFor(i, count, workload), _completion);
        if (!native) return;
        try
        {
            _pool = Native.CreatePool();
            for (int i = 0; i < count; i++) _handles[i] = GCHandle.Alloc(_items[i]);
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    internal static int IterationsFor(int index, int count, WorkloadKind workload)
    {
        if (workload == WorkloadKind.Scheduling) return 0;
        if (workload == WorkloadKind.UniformCpu || count < 8)
            return Share(TotalIterations, index, count);
        // The last 1/8 of jobs carry 7/8 of the work: a deliberate long tail.
        int lightCount = count - count / 8;
        return index < lightCount
            ? Share(TotalIterations / 8, index, lightCount)
            : Share(TotalIterations * 7 / 8, index - lightCount, count - lightCount);
    }

    private static int Share(int budget, int index, int count) => budget / count + (index < budget % count ? 1 : 0);

    internal void RunNative(int producers = 1)
    {
        _completion.Reset(_items.Length);
        _batches++;
        if (producers == 1)
            for (int i = 0; i < _items.Length; i++) SubmitNative(i);
        else ProduceConcurrently(producers, SubmitNative);
        if (!_completion.Wait(Timeout)) throw new TimeoutException("Native pool lost work or stalled");
    }

    private void SubmitNative(int index) => Native.push_value_thread_pool_ref(
        new Native.ExternRef { Ptr = GCHandle.ToIntPtr(_handles[index]) }, _pool);

    internal void RunTasks(int producers = 1)
    {
        _batches++;
        if (producers == 1)
            for (int i = 0; i < _items.Length; i++) SubmitTask(i);
        else ProduceConcurrently(producers, SubmitTask);
        if (!Task.WaitAll(_tasks, Timeout)) throw new TimeoutException("Task.Run lost work or stalled");
    }

    private void SubmitTask(int index) => _tasks[index] = Task.Run(_items[index].Action);

    internal void RunSequential()
    {
        _batches++;
        foreach (var item in _items) item.Execute();
    }

    private void ProduceConcurrently(int count, Action<int> submit)
    {
        using var start = new ManualResetEventSlim();
        Exception? failure = null;
        var threads = new Thread[count];
        for (int p = 0; p < count; p++)
        {
            int producer = p;
            threads[p] = new Thread(() =>
            {
                start.Wait();
                try
                {
                    for (int i = producer; i < _items.Length; i += count) submit(i);
                }
                catch (Exception error) { Interlocked.CompareExchange(ref failure, error, null); }
            });
            threads[p].Start();
        }
        start.Set();
        foreach (var thread in threads) thread.Join();
        if (failure != null) throw new InvalidOperationException("Producer failed", failure);
    }

    internal void Validate()
    {
        foreach (var item in _items)
            if (item.Error != null || item.Calls != _batches || (_batches > 0 && item.Result != item.Expected))
                throw new InvalidOperationException(
                    $"Job {item.Id}: expected {_batches} executions, got {item.Calls}; result {item.Result}, expected {item.Expected}", item.Error);
    }

    public void Dispose()
    {
        // Join before checking counters or freeing handles, even after timeout.
        if (_pool != 0) { Native.clean_thread_pool(_pool); _pool = 0; }
        try { Validate(); }
        finally
        {
            foreach (var handle in _handles) if (handle.IsAllocated) handle.Free();
            _completion.Dispose();
        }
    }
}

internal static unsafe partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct ExternRef { public nint Ptr; }

    static Native()
    {
        string file = OperatingSystem.IsMacOS() ? "libsleep_runtime.dylib"
            : OperatingSystem.IsWindows() ? "sleep_runtime.dll" : "libsleep_runtime.so";
        NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, (name, _, _) =>
            name == "sleep_runtime" ? NativeLibrary.Load(Path.Combine(Program.SourceDirectory(), "build", file)) : 0);
    }

    internal static void EnsureLoaded()
    {
        // Resolve the library without creating workers in the benchmark host.
        nint library = NativeLibrary.Load(Path.Combine(Program.SourceDirectory(), "build",
            OperatingSystem.IsMacOS() ? "libsleep_runtime.dylib" : OperatingSystem.IsWindows() ? "sleep_runtime.dll" : "libsleep_runtime.so"));
        NativeLibrary.Free(library);
    }

    internal static nint CreatePool()
    {
        nint pool = create_thread_pool_ref(&Callback);
        return pool != 0 ? pool : throw new InvalidOperationException("Native pool creation failed");
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Callback(ExternRef value)
    {
        var item = (WorkItem)GCHandle.FromIntPtr(value.Ptr).Target!;
        try { item.Execute(); }
        catch (Exception error) { item.Error = error; }
        finally
        {
            // Callback exceptions must never unwind through C++ worker frames.
            try { item.Completion.Signal(); }
            catch (Exception error) { item.Error = error; }
        }
    }

    [LibraryImport("sleep_runtime")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial nint create_thread_pool_ref(delegate* unmanaged[Cdecl]<ExternRef, void> worker);
    [LibraryImport("sleep_runtime")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void push_value_thread_pool_ref(ExternRef value, nint pool);
    [LibraryImport("sleep_runtime")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void clean_thread_pool(nint pool);
}

internal static class CorrectnessTests
{
    internal static void Run()
    {
        foreach (var workload in Enum.GetValues<WorkloadKind>())
        {
            int total = Enumerable.Range(0, 1024).Sum(i => Batch.IterationsFor(i, 1024, workload));
            if (total != (workload == WorkloadKind.Scheduling ? 0 : Batch.TotalIterations))
                throw new InvalidOperationException("Work distribution changed total CPU work");
        }
        foreach (bool native in new[] { false, true })
        {
            foreach (var workload in Enum.GetValues<WorkloadKind>())
            {
                using var batch = new Batch(1024, workload, native);
                for (int round = 0; round < 4; round++)
                {
                    int producers = round % 2 == 0 ? 1 : 4;
                    if (native) batch.RunNative(producers); else batch.RunTasks(producers);
                    batch.Validate();
                }
                // Exceed the native adapter's idle grace, then submit again.
                Thread.Sleep(500);
                if (native) batch.RunNative(4); else batch.RunTasks(4);
                batch.Validate();
            }
            using var single = new Batch(1, WorkloadKind.Scheduling, native);
            for (int i = 0; i < 100; i++)
            {
                if (native) single.RunNative(); else single.RunTasks();
                single.Validate();
            }
        }
        for (int i = 0; i < 25; i++) Native.clean_thread_pool(Native.CreatePool());
        Native.clean_thread_pool(0);
        Console.WriteLine("PASS: results, exactly-once execution, repeated batches, 4 producers, idle recovery, single-job handoff, and empty-pool cleanup");
    }
}
