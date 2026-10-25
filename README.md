# Satr Studio 0.9.0

Native desktop editor for **C, C++, Python, JavaScript and Java**, with Arabic,
English and French interfaces. Programs run locally in a separate interactive
console. This is a complete installer, not a patch; no previous release is needed.

## Download

**[Download Satr Studio 0.9.0](https://github.com/bibarsh39-pixel/Satr-Studio/releases/tag/v0.9.0)**

Download the complete Windows/Linux ZIP, then choose the installer for your
system inside its single `Satr-Studio` folder. The Source code buttons
on GitHub download the source project, not the installed application.

[Google Drive mirror](https://drive.google.com/file/d/1hs9CL3SlVRWNdxqjSY0HgcIQGjTkFH3J/view)
 · [دليل التثبيت بالعربية](START-HERE-AR.md) · [Build from source](BUILDING.md)

## Install

Extract the shared Windows/Linux ZIP and open its single `Satr-Studio` folder.

- Windows 10/11 x64: open `Satr-Studio-0.9.0-Windows-x64-Setup.exe`.
  GCC, clangd, Python, Node.js, Temurin JDK 21 and the optional local C/C++ assistant
  are included. Code::Blocks and a separate Java installation are not required.
- Ubuntu x86-64: open a terminal in that folder and run `bash Install-Linux.sh`.
  Keep `Satr-Studio-0.9.0-Linux-amd64.deb` beside the script. Installation needs
  administrator permission and internet to obtain missing Ubuntu packages,
  including OpenJDK 21. The build baseline is Ubuntu 24.04. This binary package
  does not cover Fedora, Arch, ARM or older Ubuntu system libraries.

Open Satr Studio from the applications menu after installation. The tools button
can install missing runtimes. User code and preferences are preserved.

## Java

Choose **New → Java**, save the template as **Main.java**, and press **F9 / Run**.
The public class and filename must match, including upper/lower case. Open an
existing `.java` file to run that file's class containing
`public static void main(String[] args)` (varargs also works).

Java uses JDK 21, including standard collections, Scanner, file I/O, networking,
Swing and the Java compiler. Imported source dependencies are compiled
automatically. For packages, keep the usual directory layout, such as
`school/Main.java` with `package school;`. Put optional JAR dependencies directly
in a `lib` directory under the source/package root. Maven/Gradle project import,
module-path builds and annotation processors such as Lombok are not configured
automatically; annotation processing is disabled in this simple Run workflow.
JavaFX is a separate library and is not bundled.

Compilation errors appear in the problem list. Click an error to open its file
and line; the original javac message remains available. Runtime exceptions and
their stack traces appear in the external console. Completion suggests Java
keywords and common standard-library names and inserts function parentheses.
It is a local suggestion list, not a Java language server.

## Performance

Java compilation runs asynchronously. The compiler JVM uses tier-1 compilation
to reduce the startup cost of short javac jobs. The program JVM keeps its normal
tiered JIT settings, so long computations can still receive full optimization.
Every Run starts a fresh program process; static state never leaks across runs.

The bounded Java cache stores verified `.class` files. Before reusing them, the
app hashes source/class/JAR dependencies and checks the JDK identity and JVM
environment. Custom JVM option environment variables disable reuse because
they can reference additional untracked files. Changed, added or deleted dependencies force recompilation. A
source edit during compilation prevents caching that result. Corrupt or
oversized cache entries fall back to compilation. Cache scanning/restoration
and writing run off the UI thread. The cache is limited to 128 MiB / 64 files.
Disabling the existing build-cache setting also disables Java reuse.

C/C++ keep the existing preprocessor/object/link cache. Python and JavaScript
keep their direct interpreter launch paths. No Java server or background JVM is
started when opening the application. Actual measured results and test scope
are in `VERIFICATION.md`; first compilation is different from execution time.

## Use and settings

Open `.c`, `.cpp`, `.py`, `.js`, `.mjs`, `.cjs` or `.java`, save, then press F9.
Use Stop to end a running program. The external console accepts keyboard input.
Choose the interface language in Settings. Quick start / Faster program controls
C/C++ compilation; Java retains normal runtime JIT behavior in either mode.

Python's Windows bundle includes the standard library, but not pip, Tkinter or
third-party packages. Linux installs the distribution's Python, pip and venv.
JavaScript runs through Node.js, without browser DOM APIs. Automatic code repair
remains available for C/C++ only. Java support does not change those workflows.

Normal Build/Run is offline. Your programs run with your account's file/network
permissions; the editor is not a security sandbox. See `SECURITY.md` and
`THIRD_PARTY_NOTICES.md` for details and component licenses.

Uninstall with Windows Settings → Apps, or `sudo apt remove satr-studio` on Ubuntu.
Source code, build instructions and verification evidence are included
as separate archives inside the download.
