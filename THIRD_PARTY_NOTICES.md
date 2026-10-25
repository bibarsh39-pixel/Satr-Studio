# Third-party notices

Satr uses the components below. Their original notice texts are retained in the `licenses` directory; this document does not replace those texts or assign a license to Satr-specific source.

## wxWidgets and its bundled components

The runtime `wxmsw32u_gcc_custom.dll` reports wxWidgets **3.2.7** and was copied unchanged from the installed Code::Blocks 25.03 Windows distribution. SHA-256: `FCD36CD7CF4863318749DC9BFA49E850FE75DE3A97A96D5E9C28ECBFA87ABE76`. Its GUI/editor APIs provide Satr's windows, controls and source editor. Satr does not link the Code::Blocks application or SDK DLL.

wxWidgets uses the wxWindows Library Licence 3.1, including its exception notice, together with the referenced GNU Library General Public Licence. See [wxWidgets-LICENSE.txt](licenses/wxWidgets-LICENSE.txt) and [wxWidgets-LGPL.txt](licenses/wxWidgets-LGPL.txt).

The matching upstream source is [wxWidgets 3.2.7](https://github.com/wxWidgets/wxWidgets/releases/tag/v3.2.7). The downloaded source archive SHA-256 is `69A1722F874D91CD1C9E742B72DF49E0FAB02890782CF794791C3104CEE868C6`.

The monolithic DLL's complete original build manifest was not supplied with the installed package. The following notices are preserved from the matching wxWidgets source for its bundled libraries; listing them does not assert that every optional codec or feature was enabled in this particular DLL.

| Component | Retained notice |
|---|---|
| Scintilla editor | [Scintilla-LICENSE.txt](licenses/Scintilla-LICENSE.txt) |
| Expat XML parser | [Expat-LICENSE.txt](licenses/Expat-LICENSE.txt) |
| libpng | [PNG-LICENSE.txt](licenses/PNG-LICENSE.txt) |
| zlib | [zlib-LICENSE.txt](licenses/zlib-LICENSE.txt) |
| PCRE2 | [PCRE-LICENSE.txt](licenses/PCRE-LICENSE.txt) |
| Independent JPEG Group software | [JPEG-README.txt](licenses/JPEG-README.txt) |
| libtiff | [TIFF-COPYRIGHT.txt](licenses/TIFF-COPYRIGHT.txt) |
| Henry Spencer regular expression package | [Henry-Spencer-REGEX-COPYRIGHT.txt](licenses/Henry-Spencer-REGEX-COPYRIGHT.txt) |

This software is based in part on the work of the Independent JPEG Group.

The Arabic standard-dialog catalog `locale/ar/LC_MESSAGES/wxstd.mo` was compiled from wxWidgets 3.2.7 `locale/ar.po`. That source states that it is distributed under the same license as wxWidgets, with copyright 2010 wxWidgets and translator credits to Abdullah Abouzekry (2010) and Fatma Mehanna (2012). The original catalog and its attribution are retained in [wxWidgets-ar-source.po](licenses/wxWidgets-ar-source.po).

## JSON for Modern C++

Satr uses [nlohmann/json 3.11.3](https://github.com/nlohmann/json/releases/tag/v3.11.3), by Niels Lohmann and contributors, under the MIT License. The single header retains its SPDX copyright and license information. The upstream license is preserved in [nlohmann-json-MIT.txt](licenses/nlohmann-json-MIT.txt) and `third_party/nlohmann/LICENSE.MIT`.

## IBM Plex Sans Arabic fonts

The `fonts` directory contains IBM Plex Sans Arabic Regular, Medium and SemiBold. The Regular font's internal metadata identifies version **1.101** and copyright **2019 IBM Corp.** The supplied license notice identifies copyright 2017 IBM Corp. and reserves the font name "Plex"; it is retained verbatim in [IBM-Plex-Sans-Arabic-OFL.txt](licenses/IBM-Plex-Sans-Arabic-OFL.txt). These fonts are distributed under the SIL Open Font License 1.1.

## Compiler runtime components

The Windows executable is built with WinLibs GCC **14.2.0**, MinGW-w64 **12.0.0**, UCRT, POSIX-thread toolchain. GCC's standard C++ and support runtimes, and needed MinGW/winpthreads runtime code, are statically linked. The Windows installer also bundles the student compiler toolchain described below.

- GCC runtime: [GCC-GPL3.txt](licenses/GCC-GPL3.txt) and [GCC-RUNTIME-EXCEPTION.txt](licenses/GCC-RUNTIME-EXCEPTION.txt). These are the upstream GCC 14.2.0 license texts and exception; the exception applies to the runtime files that carry its notice.
- MinGW-w64: [MinGW-w64-COPYING.txt](licenses/MinGW-w64-COPYING.txt), [COPYING.MinGW-w64.txt](licenses/COPYING.MinGW-w64.txt), [COPYING.MinGW-w64-runtime.txt](licenses/COPYING.MinGW-w64-runtime.txt), and [MinGW-DISCLAIMER.PD.txt](licenses/MinGW-DISCLAIMER.PD.txt). Component-specific notices in those files remain applicable.
- winpthreads: [winpthreads-COPYING.txt](licenses/winpthreads-COPYING.txt), including the mingw-w64 and Lockless notices.

These runtime texts were obtained from the exact upstream [GCC 14.2.0 source tag](https://github.com/gcc-mirror/gcc/tree/releases/gcc-14.2.0) and [MinGW-w64 12.0.0 source tag](https://github.com/mingw-w64/mingw-w64/tree/v12.0.0).

## Local repair assistant

- Qwen2.5-Coder-0.5B-Instruct-GGUF, Q4_K_M, Apache-2.0. Source: https://huggingface.co/Qwen/Qwen2.5-Coder-0.5B-Instruct-GGUF ; license: licenses/Qwen-Apache-2.0.txt.
- llama.cpp CPU runtime, MIT. Source: https://github.com/ggml-org/llama.cpp ; license: licenses/llama.cpp-MIT.txt.
- LLVM OpenMP runtime, Apache-2.0 with LLVM exceptions; license: licenses/LLVM-OpenMP.txt.

Model revision: `ebb2015119c907b064c512bf053e945850b5875f`. Model SHA256: `1d9614638d18024d0fbb36575a15f1302a3adf044df10345688ec4f6e1c4ff32`.

llama.cpp binary tag: `b11669`. Official Windows CPU archive SHA256: `37b3efcf962847ca869745fd4ccf7f5e7e8dacfeff9fa3523b4ca8cdc66d632b`. Only the completion executable and required runtime libraries are redistributed. No backend service is installed.

## Windows compiler and language server

- WinLibs GCC 14.2.0 / MinGW-w64 12.0.0 UCRT r3, including its bundled dependencies, license files and version_info.txt, preserved under toolchain/gcc-14.2.0/mingw64. Official release: https://github.com/brechtsanders/winlibs_mingw/releases/tag/14.2.0posix-12.0.0-ucrt-r3 . Project and upstream source/build information: https://winlibs.com/ and https://github.com/brechtsanders/winlibs_mingw . GCC corresponding upstream source: https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/ ; MinGW-w64 source: https://sourceforge.net/projects/mingw-w64/files/mingw-w64/mingw-w64-release/ ; the bundled version_info.txt lists the other component versions.
- LLVM clangd 19.1.2, Apache-2.0 with LLVM exceptions. Official release: https://github.com/clangd/clangd/releases/tag/19.1.2 ; corresponding source: https://github.com/llvm/llvm-project/tree/llvmorg-19.1.2 . License: licenses/LLVM-LICENSE.txt.
- Microsoft Visual C++ Redistributable is included in setup/vc_redist.x64.exe. If missing on the system, setup offers this Microsoft-signed installer and Windows requests elevation. The tools button can also download the pinned official installer. Documentation: https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist .

Exact package URLs, sizes and pinned SHA-256 values are recorded in setup/packages.json. Windows SDK/system DLLs are not copied from this development machine into the package.
## Python and JavaScript runtimes

Windows includes the official CPython 3.14.9 embeddable distribution from
https://www.python.org/ftp/python/3.14.9/python-3.14.9-embed-amd64.zip.
The Python Software Foundation license and bundled-library notices are in
`toolchain/python-3.14.9/LICENSE.txt`. Python's executable signature was verified
as valid and signed by the Python Software Foundation during packaging.
Linux uses the distribution's Python package and its installed license notices.

Windows and Linux include official Node.js v24.22.1 binaries from
https://nodejs.org/dist/v24.22.1/. Both archive SHA-256 digests were checked against
the release's SHASUMS256.txt. The complete Node.js and third-party license notices
are included in each `toolchain/node-v24.22.1-*/LICENSE` file.



## Java / Eclipse Temurin OpenJDK 21

Windows bundles Eclipse Temurin JDK 21.0.12.1+1 from the official Adoptium
temurin21-binaries release. SHA-256 and download URL are pinned in setup/packages.json.
OpenJDK is GPLv2 with the Classpath Exception where indicated; component notices
are included in toolchain/java-21/jdk-21.0.12.1+1/legal and NOTICE. The complete
JDK distribution, including its source archive, is retained.
Official source: https://github.com/adoptium/temurin21-binaries/releases/tag/jdk-21.0.12.1%2B1
Linux obtains OpenJDK 21 from signed Ubuntu packages; their licenses are installed
under /usr/share/doc/openjdk-21-* by the distribution package manager.
