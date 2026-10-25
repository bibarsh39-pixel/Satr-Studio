#pragma once
#ifndef _WIN32
#include "process_posix.hpp"
#else
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "platform.hpp"
#include <windows.h>
#include <process.h>
#include <algorithm>
#include <cerrno>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace satr {

inline std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    unsigned backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++backslashes; continue; }
        if (ch == L'\"') { result.append(backslashes * 2 + 1, L'\\'); result += ch; }
        else { result.append(backslashes, L'\\'); result += ch; }
        backslashes = 0;
    }
    result.append(backslashes * 2, L'\\');
    return result + L"\"";
}

// An owned asynchronous process tree. No shell, command interpolation or PATH
// mutation is involved; only the explicit executable and argument vector run.
class Process {
    HANDLE process_ = nullptr, output_ = nullptr, input_ = nullptr, job_ = nullptr;
    HANDLE inputThread_ = nullptr, inputWake_ = nullptr, inputStop_ = nullptr, inputIo_ = nullptr;
    CRITICAL_SECTION inputLock_;
    std::deque<std::string> inputQueue_;
    size_t pendingInput_ = 0;
    bool acceptingInput_ = false, endingInput_ = false;
    static constexpr size_t MaxPendingInput = 64 * 1024;
    DWORD code_ = 0;
    bool active_ = false;
    static void Close(HANDLE& h) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = nullptr; }

    // Only the worker touches input_ while it is running. Every write is
    // overlapped, so cancellation cannot race with the start of a blocking write.
    static unsigned __stdcall InputThread(void* owner) {
        static_cast<Process*>(owner)->WriteInput();
        return 0;
    }
    void WriteInput() {
        bool failed = false;
        while (!failed && WaitForSingleObject(inputStop_, 0) != WAIT_OBJECT_0) {
            std::string chunk;
            EnterCriticalSection(&inputLock_);
            if (!inputQueue_.empty()) {
                chunk = std::move(inputQueue_.front());
                inputQueue_.pop_front();
            }
            const bool finished = chunk.empty() && endingInput_;
            LeaveCriticalSection(&inputLock_);
            if (finished) break;
            if (chunk.empty()) {
                HANDLE events[] = {inputStop_, inputWake_};
                if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
                continue;
            }
            size_t offset = 0;
            while (offset < chunk.size()) {
                if (WaitForSingleObject(inputStop_, 0) == WAIT_OBJECT_0) { failed = true; break; }
                OVERLAPPED operation{};
                operation.hEvent = inputIo_;
                ResetEvent(inputIo_);
                DWORD written = 0;
                if (!WriteFile(input_, chunk.data() + offset,
                               static_cast<DWORD>(chunk.size() - offset), &written, &operation)) {
                    if (GetLastError() != ERROR_IO_PENDING) { failed = true; break; }
                    HANDLE events[] = {inputStop_, inputIo_};
                    const DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                    if (wait != WAIT_OBJECT_0 + 1) {
                        CancelIoEx(input_, &operation);
                        // The buffer and OVERLAPPED must remain alive until the
                        // canceled operation completes. Only this worker waits.
                        GetOverlappedResult(input_, &operation, &written, TRUE);
                        failed = true;
                        break;
                    }
                    if (!GetOverlappedResult(input_, &operation, &written, FALSE)) { failed = true; break; }
                }
                if (!written) { failed = true; break; }
                offset += written;
            }
            EnterCriticalSection(&inputLock_);
            pendingInput_ -= chunk.size();
            LeaveCriticalSection(&inputLock_);
        }
        // Closing after the queue drains sends EOF. On cancellation, pending
        // input is discarded, and no UI-thread operation waits for pipe space.
        Close(input_);
        EnterCriticalSection(&inputLock_);
        acceptingInput_ = false;
        endingInput_ = true;
        inputQueue_.clear();
        pendingInput_ = 0;
        LeaveCriticalSection(&inputLock_);
    }
    void CancelInput() {
        EnterCriticalSection(&inputLock_);
        acceptingInput_ = false;
        endingInput_ = true;
        LeaveCriticalSection(&inputLock_);
        if (inputStop_) SetEvent(inputStop_);
    }
    void ReleaseInput() {
        // Call after cancellation, or after observing that the worker exited.
        // Poll observes the worker with a zero timeout before entering here.
        if (inputThread_) WaitForSingleObject(inputThread_, INFINITE);
        Close(inputThread_); Close(input_); Close(inputWake_); Close(inputStop_); Close(inputIo_);
        EnterCriticalSection(&inputLock_);
        inputQueue_.clear();
        pendingInput_ = 0;
        acceptingInput_ = false;
        endingInput_ = false;
        LeaveCriticalSection(&inputLock_);
    }
    bool CreateInputPipe(HANDLE& childInput, SECURITY_ATTRIBUTES& inherited, std::string& error) {
        static volatile LONG serial = 0;
        const std::wstring name = L"\\\\.\\pipe\\SatrInput-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(InterlockedIncrement(&serial));
        // The parent endpoint and all worker events are noninheritable. The
        // child read endpoint alone is placed in CreateProcess's handle list.
        input_ = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED |
            FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 4096, 4096, 0, nullptr);
        if (input_ == INVALID_HANDLE_VALUE) {
            error = "Cannot create input pipe: " + std::to_string(GetLastError());
            Close(input_); return false;
        }
        inputWake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        inputStop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        inputIo_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!inputWake_ || !inputStop_ || !inputIo_) {
            error = "Cannot create input events: " + std::to_string(GetLastError());
            return false;
        }
        childInput = CreateFileW(name.c_str(), GENERIC_READ, 0, &inherited, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (childInput == INVALID_HANDLE_VALUE) {
            error = "Cannot connect input pipe: " + std::to_string(GetLastError());
            Close(childInput); return false;
        }
        OVERLAPPED connection{};
        connection.hEvent = inputIo_;
        if (!ConnectNamedPipe(input_, &connection)) {
            const DWORD result = GetLastError();
            if (result != ERROR_PIPE_CONNECTED) {
                if (result == ERROR_IO_PENDING) {
                    CancelIoEx(input_, &connection);
                    DWORD ignored = 0;
                    GetOverlappedResult(input_, &connection, &ignored, TRUE);
                }
                error = "Cannot finish input pipe connection: " + std::to_string(result);
                return false;
            }
        }
        return true;
    }
public:
    Process() { InitializeCriticalSection(&inputLock_); }
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    ~Process() {
        Stop(); ReleaseInput(); Close(process_); Close(output_); Close(job_);
        DeleteCriticalSection(&inputLock_);
    }
    bool Active() const { return active_; }
    DWORD ExitCode() const { return code_; }

    bool Start(const std::wstring& executable, const std::vector<std::wstring>& args,
               const std::wstring& workingDirectory, std::string& error) {
        if (active_) { error = "A process is already running"; return false; }
        error.clear();
        CancelInput(); ReleaseInput(); Close(process_); Close(output_); Close(job_);
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE childOutput = nullptr, childInput = nullptr;
        if (!CreatePipe(&output_, &childOutput, &sa, 0)) {
            error = "CreatePipe failed: " + std::to_string(GetLastError());
            Close(childOutput); Close(output_); return false;
        }
        if (!SetHandleInformation(output_, HANDLE_FLAG_INHERIT, 0) || !CreateInputPipe(childInput, sa, error)) {
            if (error.empty()) error = "Cannot restrict output pipe inheritance: " + std::to_string(GetLastError());
            Close(childOutput); Close(childInput); Close(output_); ReleaseInput(); return false;
        }
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            error = "Cannot create owned process job";
            Close(childOutput); Close(childInput); Close(output_); ReleaseInput(); Close(job_); return false;
        }
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<unsigned char> attributes(size);
        STARTUPINFOEXW start{};
        start.StartupInfo.cb = sizeof(start);
        start.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        start.StartupInfo.wShowWindow = SW_HIDE;
        start.StartupInfo.hStdInput = childInput;
        start.StartupInfo.hStdOutput = childOutput;
        start.StartupInfo.hStdError = childOutput;
        start.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        HANDLE inherited[] = {childInput, childOutput};
        if (!InitializeProcThreadAttributeList(start.lpAttributeList, 1, 0, &size)) {
            error = "Cannot initialize process attributes";
            Close(childOutput); Close(childInput); Close(output_); ReleaseInput(); Close(job_); return false;
        }
        if (!UpdateProcThreadAttribute(start.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       inherited, sizeof(inherited), nullptr, nullptr)) {
            error = "Cannot restrict inherited handles";
            DeleteProcThreadAttributeList(start.lpAttributeList);
            Close(childOutput); Close(childInput); Close(output_); ReleaseInput(); Close(job_); return false;
        }
        std::wstring command = QuoteArgument(executable);
        for (const auto& arg : args) command += L" " + QuoteArgument(arg);
        PROCESS_INFORMATION pi{};
        const BOOL success = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &start.StartupInfo, &pi);
        const DWORD lastError = GetLastError();
        DeleteProcThreadAttributeList(start.lpAttributeList);
        Close(childInput); Close(childOutput);
        if (!success) {
            error = "CreateProcess failed: " + std::to_string(lastError);
            Close(output_); ReleaseInput(); Close(job_); return false;
        }
        if (!AssignProcessToJobObject(job_, pi.hProcess)) {
            error = "Cannot attach process to owned job: " + std::to_string(GetLastError());
            TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            Close(output_); ReleaseInput(); Close(job_); return false;
        }
        process_ = pi.hProcess;
        EnterCriticalSection(&inputLock_);
        acceptingInput_ = true;
        endingInput_ = false;
        LeaveCriticalSection(&inputLock_);
        inputThread_ = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, InputThread, this, 0, nullptr));
        if (!inputThread_) error = "Cannot start process input worker: " + std::to_string(errno);
        else if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1))
            error = "Cannot resume process: " + std::to_string(GetLastError());
        if (!error.empty()) {
            TerminateJobObject(job_, 1);
            CancelInput(); ReleaseInput();
            CloseHandle(pi.hThread); Close(process_); Close(output_); Close(job_);
            return false;
        }
        active_ = true;
        code_ = STILL_ACTIVE;
        CloseHandle(pi.hThread);
        return true;
    }

    std::string Poll() {
        std::string text;
        if (!active_) return text;
        // A bounded read keeps a program producing endless output from freezing
        // the editor. Remaining bytes are drained by subsequent timer ticks.
        for (int i = 0; i < 16; ++i) {
            DWORD available = 0, received = 0;
            if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr) || !available) break;
            char buffer[4096];
            if (!ReadFile(output_, buffer, std::min<DWORD>(available, sizeof(buffer)), &received, nullptr) || !received) break;
            text.append(buffer, received);
        }
        if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
            DWORD available = 0;
            if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr) || !available) {
                GetExitCodeProcess(process_, &code_);
                CancelInput();
                Close(job_);
                // A pending input write is canceled asynchronously. Defer final
                // cleanup to a later timer tick rather than waiting in the UI.
                if (inputThread_ && WaitForSingleObject(inputThread_, 0) != WAIT_OBJECT_0) return text;
                ReleaseInput();
                active_ = false;
                Close(output_); Close(process_);
            }
        }
        return text;
    }

    bool Send(const std::string& text) {
        // True means accepted into the bounded queue; a closed/full input
        // returns false immediately. Delivery happens on the owned worker.
        if (!active_ || text.size() > 4096) return false;
        EnterCriticalSection(&inputLock_);
        bool accepted = acceptingInput_ && text.size() <= MaxPendingInput - pendingInput_;
        if (accepted && !text.empty()) {
            try { inputQueue_.push_back(text); pendingInput_ += text.size(); }
            catch (...) { accepted = false; }
        }
        LeaveCriticalSection(&inputLock_);
        if (accepted && inputWake_) SetEvent(inputWake_);
        return accepted;
    }
    void EndInput() {
        EnterCriticalSection(&inputLock_);
        acceptingInput_ = false;
        endingInput_ = true;
        LeaveCriticalSection(&inputLock_);
        if (inputWake_) SetEvent(inputWake_);
    }
    void Stop() {
        if (active_ && job_) TerminateJobObject(job_, 130);
        CancelInput();
    }
};
}

#endif
