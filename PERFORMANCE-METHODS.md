# Run performance: research and decisions

Reviewed 24 October 2026. These are documented techniques, not a benchmark
claim that Satr Studio is faster than VS Code or CLion.

| Technique | Primary source | Satr Studio decision |
| --- | --- | --- |
| Reuse a terminal between tasks; launch a tool directly without a shell | [VS Code tasks](https://code.visualstudio.com/docs/debugtest/tasks) | Reuse the external terminal, preserve scrollback, and launch explicit executables with argument vectors. Prepare the Windows terminal in the background. |
| Avoid rebuilding unchanged inputs | [CLion Makefiles](https://www.jetbrains.com/help/clion/makefiles-support.html) | Keep checked C/C++ object reuse. Compare the compiler's expanded source, flags, compiler identity, environment and link dependencies; changed headers and optimization settings invalidate reuse. |
| Precompiled headers | [CLion/CMake PCH](https://blog.jetbrains.com/clion/2020/07/clion-2020-2-makefile-cpp20-doctest/), [GCC requirements](https://gcc.gnu.org/onlinedocs/gcc/Precompiled-Headers.html) | Not forced globally. PCH construction has an initial cost and requires matching flags, macros and headers. It can benefit repeated project builds, but does not remove the first compilation cost. Existing external PCH inputs are conservatively excluded from object-cache reuse. |
| Smaller generated executables through shared runtime linking | [GCC link options](https://gcc.gnu.org/onlinedocs/gcc/Link-Options.html), [Windows DLL search](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order) | Stage the selected compiler's runtime DLLs in the private build directory. Fall back to static linking when unavailable. Copy/validation runs alongside compilation. No global PATH changes. |
| Cache JavaScript compilation for larger module graphs | [Node module compile cache](https://nodejs.org/api/module.html#module-compile-cache) | Retain the supported compile-cache API in bundled Node 24 for larger files/projects. Small scripts launch directly. A cache can add first-load cost; every Run still starts a fresh process. |
| Python bytecode caches | [Python 3.14 modules](https://docs.python.org/3.14/tutorial/modules.html#compiled-python-files) | Imported modules use Python's normal cache. The main script retains normal execution semantics and a fresh process. Bytecode caching accelerates loading, not the program's algorithm. Assertions are preserved. |

Other experiments: stripping only debug symbols did not consistently help the
Inventory example. Clang/LLD variants were tried locally, but did not establish a
consistent first-build advantage that justified replacing the bundled compiler
and diagnostic handling. No alternate compiler is required by this release.

Quick mode prioritizes short edit/build/run cycles. Optimized mode uses the
compiler's normal `-O2` for CPU-intensive native programs, at the cost of longer
compilation. There is no single setting that minimizes both compile time and
runtime for every arbitrary program. No unsafe math flags, persistent user-code
interpreter, disabled assertions, antivirus exclusions or skipped errors are used.

See PERFORMANCE-0.8.1.md for measured results, including slower cases and the
distinction between first compilation and repeated execution.
