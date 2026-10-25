#pragma once
#include <string>
#include <filesystem>
#include <chrono>
#include <codecvt>
#include <locale>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
using DWORD = unsigned long;
using ULONGLONG = unsigned long long;
inline ULONGLONG GetTickCount64() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline DWORD GetCurrentProcessId() { return static_cast<DWORD>(getpid()); }
#endif
namespace satr {
inline std::string ToUtf8(const std::wstring& value) {
#ifdef _WIN32
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().to_bytes(value);
#else
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(value);
#endif
}
inline std::wstring ToWide(const std::string& value) {
#ifdef _WIN32
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(value);
#else
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(value);
#endif
}
inline std::filesystem::path FsPath(const std::wstring& value) {
#ifdef _WIN32
    return std::filesystem::path(value);
#else
    return std::filesystem::u8path(ToUtf8(value));
#endif
}
inline std::wstring WidePath(const std::filesystem::path& value) {
#ifdef _WIN32
    return value.wstring();
#else
    return ToWide(value.u8string());
#endif
}
}
