# Satr Studio 0.9.0 verification

Verified 24 October 2026. This is a full Windows/Ubuntu distribution with Java
added alongside C, C++, Python and JavaScript. No previous release is needed.

| Check | Result |
| --- | --- |
| Windows application: five languages | 81 passed |
| Windows application: editor | 49 passed |
| Windows full installer: first session | 81 passed |
| Windows full installer: restarted session | 81 passed |
| Windows full installer: editor | 49 passed |
| Ubuntu package: five languages | 81 passed |
| Ubuntu package: editor | 46 passed |
| Windows Java compiler/cache/dependencies/Unicode | 47 passed |
| Ubuntu Java compiler/cache/dependencies/Unicode | 47 passed |
| Windows real external terminals: Python, JavaScript, Java | 3 passed |
| Ubuntu real external terminals: Python, JavaScript, Java | 3 passed |
| Setup download/archive validation | 29 passed |
| Windows bundled runtime setup without network | Passed, all five languages |
| Application performance/output checks | 16 programs, 7 runs each |

Java checks exercise real javac/java processes, packages, sibling source files,
JAR dependencies, changed and removed dependencies, same-size/timestamp edits,
interactive Scanner input, Unicode filenames and class names, classpath resource
loading, error locations, corrupt cache fallback and completion parentheses.
Windows uses a standard manifest classpath JAR to preserve non-ANSI paths while
retaining the normal Java application class loader. The compiler reads UTF-8
argument files. Each run starts a fresh JVM with normal program JIT settings.

The Windows installer was tested in a new isolated directory with PATH limited
to Windows system folders, using bundled GCC, Python, Node.js and JDK 21. Payload
hashes were checked. Restarting the application confirmed persistent C++ and
Java cache reuse. Uninstall preserved a student-created file and the existing
user installation registration. This is not a fresh Windows virtual machine;
system-wide libraries already present on the host were still available.

The Ubuntu package was installed into a separate Ubuntu 24.04 chroot. Required
OpenJDK 21 packages were installed through apt. GUI checks ran as an unprivileged
user under Xvfb. Existing non-Java dependencies were already installed. Linux
requires the dependencies declared by the .deb and is not a universal binary
for every distribution or architecture.

See PERFORMANCE-0.9.0.md for timing scope and results. Source builds succeeded
on Windows and Ubuntu. Archive integrity and SHA-256 checks cover the installers,
source and evidence. The installers are not publisher-signed. SECURITY.md
describes the security review scope; this is not an independent security audit.
Normal Run is offline, but student programs run with the user's permissions.
