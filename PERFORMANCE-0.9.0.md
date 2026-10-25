# Satr Studio 0.9.0 performance

Measured on the development Windows x64 PC, 24 October 2026. Results vary by
hardware, OS caches, antivirus and program workload. The first result uses an
empty application build cache, not a freshly rebooted operating system.

Timing starts at the application's actual Build/Run action and includes source
checks, compilation when required, process launch, output and UI completion
detection. Opening the editor is excluded. The reusable terminal is prepared
before the timed action. Each program runs in a fresh child process. These are
end-to-end measurements, not just the time printed inside the console.

Each of 16 programs has one output-verification control and six external-console
runs. The first external run has no application build cache. Repeat results are
the median of the following five, not the fastest. Runs were sequential without
simultaneous packaging or build jobs. Expected outputs and exit codes passed.

| Program | Source lines | First build + run (s) | Repeated median (s) |
| --- | ---: | ---: | ---: |
| hello.c | 2 | 0.828 | 0.187 |
| hello.cpp | 2 | 1.141 | 0.343 |
| hello.py | 1 | 0.079 | 0.094 |
| hello.js | 1 | 0.125 | 0.125 |
| JavaSmall.java | 5 | 0.984 | 0.188 |
| JavaComplex.java | 42 | 1.578 | 0.406 |
| user_inventory.cpp | 79 | 1.657 | 0.422 |
| c_complex.c | 26 | 0.734 | 0.219 |
| cpp_complex.cpp | 25 | 9.015 | 0.500 |
| python_complex.py | 31 | 0.563 | 0.562 |
| javascript_complex.js | 23 | 0.187 | 0.203 |
| large.c | 4003 | 3.219 | 0.219 |
| large.cpp | 4003 | 4.922 | 0.453 |
| large.py | 6004 | 0.266 | 0.172 |
| large.js | 4005 | 0.250 | 0.140 |
| JavaLarge.java | 4005 | 1.344 | 0.203 |

Java uses JDK 21. Only javac uses `-XX:TieredStopAtLevel=1` to reduce compiler
startup overhead. Student programs retain normal tiered JIT optimization. The
persistent class cache is content-validated against source, class and JAR inputs
and JDK identity; custom JVM option environments disable reuse. The unit tests
check dependency edits with unchanged timestamps, removed JARs, corrupt caches
and changes made during compilation. Java has no persistent interpreter daemon.

C/C++ retain their previous object/link cache and private runtime DLL support.
Python and JavaScript retain their direct runtime launch paths. Small-program
latency and long-running algorithms are different costs: the editor cannot
guarantee a fixed execution time for arbitrary code, input or external services.

Full samples, output controls and cache counters are in the verification archive.
The older performance reports describe their own historical versions and tests.
