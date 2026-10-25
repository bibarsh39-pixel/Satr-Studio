# Building Satr Studio

Satr Studio is a native C++17 application built with wxWidgets 3.2. The source
archive contains the editor, console runners, setup helpers, tests, fonts and
translations. Compiler toolchains and optional model binaries are distributed
in the installers, not in the source repository.

## Ubuntu 24.04

Install the build dependencies:

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build libwxgtk3.2-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
cp -r fonts locale examples licenses setup build/
cp SECURITY.md THIRD_PARTY_NOTICES.md build/
./build/Satr
```

Install the programming tools with `bash setup/SetupTools-Linux.sh` if needed.
The editor can use system-installed GCC, Python, Node.js and OpenJDK 21. The
optional local repair model is not required for editing, compiling or running.

## Windows x64

Use a MinGW-w64 C++17 compiler and matching wxWidgets 3.2.7 Unicode headers,
configuration headers, import library and runtime DLL. They must share the same
compiler ABI and wxWidgets build configuration. GCC 14.2 was used for the release.

Run PowerShell from the source folder, replacing the dependency paths:

```powershell
.\build.ps1 `
  -Compiler 'C:\deps\mingw64\bin\g++.exe' `
  -WxInclude 'C:\deps\wx\include' `
  -WxConfig 'C:\deps\wx\lib\gcc_dll\mswu' `
  -WxLib 'C:\deps\wx\lib\gcc_dll\libwxmsw32u.a' `
  -WxRuntime 'C:\deps\wx\lib\gcc_dll\wxmsw32u_gcc_custom.dll' `
  -BuildDirectory 'C:\build\satr' `
  -OutputDirectory '.\bin'
```

See `Get-Help .\build.ps1 -Full` for ABI details. The script builds the editor
and runner without downloading dependencies. Use **Install tools** in the app
to install the pinned Windows runtime packages. `setup/packages.json` contains
their upstream URLs, byte sizes and SHA-256 values.

## Tests and release files

The `tests` folder contains compiler, completion, process, console, cache and
download-validation checks. The five-language examples used for integration
checks are in `tests/four_languages`. `tools/benchmark-app-090.py` measures the
Windows app's actual Build/Run path and checks expected program output.

`VERIFICATION.md` records the tested environments and limitations. Installer
builds also need the complete runtime payload; building the editor alone does
not recreate that payload. The packaged binaries are not claimed to be
bit-for-bit reproducible from a different machine or dependency build.

Third-party components retain their licenses in `licenses` and
`THIRD_PARTY_NOTICES.md`. No additional license is granted to Satr-specific code
by those third-party notices.
