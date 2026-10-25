// Native end-to-end process runner checks. Compile with -municode.
// Usage: process_test.exe <scratch-directory> <absolute-g++-path>
// Use -static for this harness: the installed WinLibs compiler otherwise emits
// executables requiring MinGW runtime DLLs outside the normal Windows search path.
#include "../src/process.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

static std::wstring Self() {
    wchar_t path[32768];
    DWORD size = GetModuleFileNameW(nullptr, path, 32768);
    return std::wstring(path, size);
}

static void Flush(const std::string& value) {
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), value.data(), static_cast<DWORD>(value.size()), &written, nullptr);
}

// Helpers are actual child processes, not mocks of Process.
static int Helper(int argc, wchar_t** argv) {
    const std::wstring mode = argv[1];
    if (mode == L"--sleep" || mode == L"--tree-leaf") { Sleep(60000); return 0; }
    if (mode == L"--exit") { Flush("nonzero sentinel\n"); return 37; }
    if (mode == L"--flood") {
        const std::string chunk(4096, 'F');
        for (int i = 0; i < 512; ++i) Flush(chunk);
        Flush("END-OF-FLOOD\n");
        return 0;
    }
    if (mode == L"--endless") {
        const std::string chunk(4096, 'X');
        while (true) Flush(chunk);
    }
    if (mode == L"--tree-root") {
        std::wstring executable = Self();
        std::wstring command = satr::QuoteArgument(executable) + L" --tree-leaf";
        STARTUPINFOW start{}; start.cb = sizeof(start);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &start, &pi)) return 91;
        Flush("CHILD=" + std::to_string(pi.dwProcessId) + "\n");
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        Sleep(60000);
        return 0;
    }
    if (mode == L"--stdin-probe") {
        satr::Process target;
        std::string error;
        if (!target.Start(Self(), {L"--sleep"}, L"", error)) return 92;
        Flush("BEGIN-SEND\n");
        for (int i = 0; i < 100; ++i) {
            if (!target.Send(std::string(4096, 'I'))) break;
        }
        Flush("SEND-RETURNED\n");
        target.Stop();
        Flush("STOP-RETURNED\n");
        return 0;
    }
    if (mode == L"--args") {
        const std::vector<std::wstring> expected = {L"", L"word with spaces", L"quote\"inside", L"C:\\path with spaces\\", L"العربية"};
        if (argc != static_cast<int>(expected.size()) + 2) return 93;
        for (size_t i = 0; i < expected.size(); ++i) if (argv[i+2] != expected[i]) return 94;
        Flush("ARGS-OK\n");
        return 0;
    }
    return 95;
}

struct Result { DWORD code; std::string output; ULONGLONG maxPollMs; };

static Result Finish(satr::Process& process, DWORD timeoutMs = 15000) {
    const ULONGLONG started = GetTickCount64();
    Result result{0, "", 0};
    while (process.Active()) {
        const ULONGLONG before = GetTickCount64();
        result.output += process.Poll();
        result.maxPollMs = (std::max)(result.maxPollMs, GetTickCount64() - before);
        if (GetTickCount64() - started > timeoutMs) {
            process.Stop();
            for (int i = 0; i < 500 && process.Active(); ++i) { process.Poll(); Sleep(2); }
            throw std::runtime_error("process exceeded watchdog deadline");
        }
        if (process.Active()) Sleep(1);
    }
    result.code = process.ExitCode();
    return result;
}

static void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static void Start(satr::Process& process, const std::wstring& exe,
                  const std::vector<std::wstring>& args, const fs::path& cwd) {
    std::string error;
    Require(process.Start(exe, args, cwd.wstring(), error), "Start failed: " + error);
}

template<class F> static bool Check(const char* name, F&& run) {
    try { run(); std::cout << "PASS " << name << std::endl; return true; }
    catch (const std::exception& error) { std::cout << "FAIL " << name << ": " << error.what() << std::endl; return false; }
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && std::wstring(argv[1]).rfind(L"--", 0) == 0) return Helper(argc, argv);
    if (argc != 3) { std::cerr << "usage: process_test.exe scratch-directory g++-path\n"; return 2; }
    const fs::path scratch = fs::absolute(argv[1]);
    const std::wstring compiler = argv[2];
    fs::create_directories(scratch);
    int failed = 0;

    failed += !Check("real GCC compilation / Arabic + spaced paths / stdin stdout / EOF", [&] {
        const fs::path directory = scratch / L"مسار بمسافات";
        fs::create_directories(directory);
        const fs::path source = directory / L"برنامج تجربة.cpp";
        const fs::path output = directory / L"برنامج تجربة.exe";
        const fs::path linked = scratch / L"unicode-source-program.exe";
        std::ofstream(source, std::ios::binary) << "#include <iostream>\n#include <string>\nint main(){std::string x;std::cout<<\"READY\\n\"<<std::flush;while(std::getline(std::cin,x))std::cout<<\"ECHO:\"<<x<<'\\n';std::cout<<\"EOF\\n\";return 0;}\n";
        satr::Process build;
        // This installed ld.exe cannot open Arabic -o paths, even though GCC
        // reads Arabic source paths. Link in ASCII then use Unicode file APIs.
        Start(build, compiler, {L"-std=c++17", L"-O0", L"-static", source.wstring(), L"-o", linked.wstring()}, directory);
        const Result compiled = Finish(build, 60000);
        Require(compiled.code == 0 && fs::exists(linked), "real compilation failed: " + compiled.output);
        fs::copy_file(linked, output, fs::copy_options::overwrite_existing);
        satr::Process run;
        Start(run, output.wstring(), {}, directory);
        const std::string input = u8"hello from Satr | مرحبا من سطر";
        Require(run.Send(input + "\n"), "stdin write failed");
        run.EndInput();
        const Result ran = Finish(run);
        Require(ran.code == 0, "program nonzero exit");
        std::string normalized = ran.output;
        normalized.erase(std::remove(normalized.begin(), normalized.end(), '\r'), normalized.end());
        Require(normalized == "READY\nECHO:" + input + "\nEOF\n", "stdout / stdin / EOF mismatch: " + ran.output);
    });

    failed += !Check("actual C compilation and interactive stdin", [&] {
        const fs::path source = scratch / L"C program.c";
        const fs::path output = scratch / L"C program.exe";
        const fs::path gcc = fs::path(compiler).parent_path() / L"gcc.exe";
        std::ofstream(source, std::ios::binary) << "#include <stdio.h>\nint main(void){int n;if(scanf(\"%d\",&n)!=1)return 8;printf(\"DOUBLE:%d\\n\",n*2);return 0;}\n";
        satr::Process build;
        Start(build, gcc.wstring(), {L"-std=c11", L"-static", source.wstring(), L"-o", output.wstring()}, scratch);
        const Result compiled = Finish(build, 60000);
        Require(compiled.code == 0, "real C compilation failed: " + compiled.output);
        satr::Process run;
        Start(run, output.wstring(), {}, scratch);
        Require(run.Send("21\n"), "C stdin write failed");
        run.EndInput();
        const Result ran = Finish(run);
        Require(ran.code == 0 && ran.output.find("DOUBLE:42") != std::string::npos, "C program failed input/output");
    });

    failed += !Check("actual GCC error and nonzero compilation exit", [&] {
        const fs::path source = scratch / L"invalid source.cpp";
        std::ofstream(source, std::ios::binary) << "int main() { missing_symbol = 1; }\n";
        satr::Process build;
        Start(build, compiler, {L"-fdiagnostics-format=json", L"-fsyntax-only", source.wstring()}, scratch);
        const Result result = Finish(build, 60000);
        Require(result.code != 0, "invalid source incorrectly succeeded");
        Require(result.output.find("missing_symbol") != std::string::npos && result.output.find("\"line\": 1") != std::string::npos, "compiler diagnostic/source location missing: " + result.output);
    });

    failed += !Check("literal argument boundary preservation", [&] {
        satr::Process process;
        Start(process, Self(), {L"--args", L"", L"word with spaces", L"quote\"inside", L"C:\\path with spaces\\", L"العربية"}, scratch);
        const Result result = Finish(process);
        Require(result.code == 0 && result.output.find("ARGS-OK") != std::string::npos, "child observed changed argument vector");
    });

    failed += !Check("runtime nonzero exit preserves final output", [&] {
        satr::Process process;
        Start(process, Self(), {L"--exit"}, scratch);
        const Result result = Finish(process);
        Require(result.code == 37 && result.output == "nonzero sentinel\n", "exit code or final output lost");
    });

    failed += !Check("2 MiB output drains fully with bounded Poll", [&] {
        satr::Process process;
        Start(process, Self(), {L"--flood"}, scratch);
        const Result result = Finish(process);
        Require(result.code == 0 && result.output == std::string(2 * 1024 * 1024, 'F') + "END-OF-FLOOD\n", "flood output truncated or corrupted");
        Require(result.maxPollMs < 250, "Poll exceeded 250 ms: " + std::to_string(result.maxPollMs));
        std::cout << "  max flood Poll: " << result.maxPollMs << " ms\n";
    });

    failed += !Check("endless-output program remains cancellable", [&] {
        satr::Process process;
        Start(process, Self(), {L"--endless"}, scratch);
        for (int i = 0; i < 10; ++i) { const ULONGLONG before = GetTickCount64(); process.Poll(); Require(GetTickCount64()-before < 250, "endless Poll stalled"); Sleep(2); }
        const ULONGLONG before = GetTickCount64();
        process.Stop();
        Finish(process, 3000);
        Require(GetTickCount64() - before < 3000, "cancellation took too long");
    });

    failed += !Check("rapid cancellation and same-object restart", [&] {
        satr::Process process;
        for (int i = 0; i < 8; ++i) {
            Start(process, Self(), {L"--sleep"}, scratch);
            process.Stop();
            Finish(process, 3000);
            Require(process.ExitCode() == 130, "unexpected forced-stop exit code");
        }
    });

    failed += !Check("Stop terminates descendant process tree", [&] {
        satr::Process process;
        Start(process, Self(), {L"--tree-root"}, scratch);
        std::string text;
        const ULONGLONG before = GetTickCount64();
        while (text.find('\n') == std::string::npos && GetTickCount64()-before < 3000) { text += process.Poll(); Sleep(2); }
        Require(text.rfind("CHILD=", 0) == 0, "no descendant PID reported");
        const DWORD pid = static_cast<DWORD>(std::stoul(text.substr(6)));
        HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, pid);
        Require(child != nullptr, "cannot inspect owned descendant liveness");
        process.Stop(); Finish(process, 3000);
        const DWORD wait = WaitForSingleObject(child, 3000);
        CloseHandle(child);
        Require(wait == WAIT_OBJECT_0, "owned descendant survived Stop");
    });

    failed += !Check("stdin backpressure never blocks caller", [&] {
        satr::Process probe;
        Start(probe, Self(), {L"--stdin-probe"}, scratch);
        std::string text;
        const ULONGLONG before = GetTickCount64();
        while (probe.Active() && GetTickCount64()-before < 1500) { text += probe.Poll(); Sleep(2); }
        const bool returned = text.find("SEND-RETURNED") != std::string::npos;
        probe.Stop();
        Finish(probe, 3000);
        Require(text.find("BEGIN-SEND") != std::string::npos, "stdin probe did not start");
        Require(returned, "Send blocked >1500 ms when child never reads stdin; outer job watchdog cleaned up probe and child");
        Require(text.find("STOP-RETURNED") != std::string::npos, "Stop stalled with stdin backpressure");
    });

    std::cout << "RESULT " << (failed ? "FAILED" : "PASSED") << ": " << failed << " failing tests\n";
    return failed ? 1 : 0;
}
