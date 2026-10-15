#:property PublishAot=true
#:property AllowUnsafeBlocks=true
#:property TieredCompilation=false

// Requires .NET 10 and the root project's C++26 toolchain. From this folder:
// make run                       # JIT benchmark
// make run-aot                   # Native AOT benchmark
// make bench ARGS="44 1000000 5"  # Both, with a custom workload
// make test                      # Quick correctness checks on both builds

using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

static unsafe partial class Benchmark
{
    [StructLayout(LayoutKind.Sequential)]
    struct ExternRef { public nint Ptr; }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static void Callback(ExternRef value)
    {
        var job = (Job)GCHandle.FromIntPtr(value.Ptr).Target!;
        try { job.Run(); }
        catch (Exception error) { job.Error = error; }
        finally { job.Done!.Signal(); }
    }

    [LibraryImport("sleep_runtime")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial nint create_thread_pool_ref(delegate* unmanaged[Cdecl]<ExternRef, void> worker);
    [LibraryImport("sleep_runtime")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial void push_value_thread_pool_ref(ExternRef value, nint pool);
    [LibraryImport("sleep_runtime")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    private static partial void clean_thread_pool(nint pool);

    sealed class Job(int start, int count)
    {
        public int Result, Calls;
        public CountdownEvent? Done;
        public Exception? Error;
        public void Run()
        {
            Result = CountPrimes(start, count);
            Interlocked.Increment(ref Calls);
        }
    }

    static int CountPrimes(int start, int count)
    {
        int primes = 0;
        for (int n = start; n < start + count; n++)
        {
            if (n > 2 && n % 2 == 0) continue;
            bool prime = true;
            for (int d = 3; (long)d * d <= n; d += 2)
                if (n % d == 0) { prime = false; break; }
            if (prime) primes++;
        }
        return primes;
    }

    static void RunNative(Job[] jobs)
    {
        using var done = new CountdownEvent(jobs.Length);
        var handles = new GCHandle[jobs.Length];
        nint pool = create_thread_pool_ref(&Callback);
        if (pool == 0) throw new InvalidOperationException("Native pool creation failed");
        try
        {
            for (int i = 0; i < jobs.Length; i++)
            {
                jobs[i].Done = done;
                handles[i] = GCHandle.Alloc(jobs[i]);
                push_value_thread_pool_ref(new ExternRef { Ptr = GCHandle.ToIntPtr(handles[i]) }, pool);
            }
            if (!done.Wait(TimeSpan.FromMinutes(5)))
                throw new TimeoutException("Native workers did not finish within five minutes");
        }
        finally
        {
            // Joining before freeing handles also makes the timeout path safe.
            clean_thread_pool(pool);
            foreach (var handle in handles)
                if (handle.IsAllocated) handle.Free();
        }
    }

    static double Measure(bool native, int count, int chunk, int[] expected)
    {
        var jobs = Enumerable.Range(0, count).Select(i => new Job(2 + i * chunk, chunk)).ToArray();
        var watch = Stopwatch.StartNew();
        if (native) RunNative(jobs);
        else Task.WaitAll(jobs.Select(job => Task.Run(job.Run)).ToArray());
        watch.Stop();
        for (int i = 0; i < count; i++)
            if (jobs[i].Error != null || jobs[i].Calls != 1 || jobs[i].Result != expected[i])
                throw new InvalidOperationException($"Job {i} failed correctness/exactly-once check", jobs[i].Error);
        return watch.Elapsed.TotalMilliseconds;
    }

    static string SourceDirectory([CallerFilePath] string file = "") => Path.GetDirectoryName(file)!;

    static void Main(string[] args)
    {
        int jobs = args.Length > 0 ? int.Parse(args[0]) : Environment.ProcessorCount * 4;
        int chunk = args.Length > 1 ? int.Parse(args[1]) : 1_000_000;
        int rounds = args.Length > 2 ? int.Parse(args[2]) : 5;
        if (args.Length > 3 || jobs <= 0 || chunk <= 0 || rounds <= 0 || (long)jobs * chunk > int.MaxValue - 2)
            throw new ArgumentException("Usage: Benchmark.cs [positive jobs] [positive numbersPerJob] [positive rounds]");

        string library = OperatingSystem.IsMacOS() ? "libsleep_runtime.dylib"
            : OperatingSystem.IsWindows() ? "sleep_runtime.dll" : "libsleep_runtime.so";
        NativeLibrary.SetDllImportResolver(typeof(Benchmark).Assembly, (name, _, _) =>
            name == "sleep_runtime" ? NativeLibrary.Load(Path.Combine(SourceDirectory(), "build", library)) : 0);

        Console.WriteLine($"{Environment.ProcessorCount} logical CPUs; {jobs} jobs x {chunk:N0} numbers; {rounds} rounds");
        Console.WriteLine("Same C# prime-counting function on both pools; checking against a sequential reference.");
        int[] expected = Enumerable.Range(0, jobs).Select(i => CountPrimes(2 + i * chunk, chunk)).ToArray();
        Measure(false, jobs, chunk, expected);
        Measure(true, jobs, chunk, expected);

        var nativeTimes = new double[rounds];
        var taskTimes = new double[rounds];
        Console.WriteLine("Round    native (ms)    Task.Run (ms)");
        for (int i = 0; i < rounds; i++)
        {
            // No native pool remains alive while Task.Run is measured.
            if (i % 2 == 0)
            {
                nativeTimes[i] = Measure(true, jobs, chunk, expected);
                taskTimes[i] = Measure(false, jobs, chunk, expected);
            }
            else
            {
                taskTimes[i] = Measure(false, jobs, chunk, expected);
                nativeTimes[i] = Measure(true, jobs, chunk, expected);
            }
            Console.WriteLine($"{i + 1,5} {nativeTimes[i],14:F2} {taskTimes[i],16:F2}");
        }
        double Median(double[] times)
        {
            Array.Sort(times);
            int middle = times.Length / 2;
            return times.Length % 2 == 0 ? (times[middle - 1] + times[middle]) / 2 : times[middle];
        }
        double nativeMedian = Median(nativeTimes), taskMedian = Median(taskTimes);
        Console.WriteLine($"Median {nativeMedian,14:F2} {taskMedian,16:F2}");
        Console.WriteLine($"Task.Run/native time: {taskMedian / nativeMedian:F2}x (>1 favors native)");
        Console.WriteLine($"PASS: all jobs executed once; {expected.Sum():N0} primes per run.");
        Console.WriteLine("Times include submission/waiting and native pool creation/destruction; Task.Run uses .NET's shared pool.");
    }
}
