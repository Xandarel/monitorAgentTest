#include "collector.hpp"

#include "encoding.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <ctime>

namespace {

constexpr DWORD kActivityWindowMs = 5000;

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
    ~ScopedHandle() {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    HANDLE get() const { return handle_; }
    explicit operator bool() const { return handle_ && handle_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE handle_;
};

std::string format_local_time(std::time_t value) {
    std::tm time_info{};
    if (localtime_s(&time_info, &value) != 0) {
        return "1970-01-01 00:00:00";
    }
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &time_info) == 0) {
        return "1970-01-01 00:00:00";
    }
    return buffer;
}

bool user_was_active() {
    LASTINPUTINFO info{};
    info.cbSize = sizeof(info);
    if (!GetLastInputInfo(&info)) {
        return false;
    }
    // Оба значения — DWORD от GetTickCount, вычитание корректно при переполнении счётчика.
    const DWORD idle_ms = GetTickCount() - info.dwTime;
    return idle_ms <= kActivityWindowMs;
}

std::string file_name_from_path(std::wstring_view path) {
    const auto pos = path.find_last_of(L"\\/");
    const std::wstring_view file = pos == std::wstring_view::npos ? path : path.substr(pos + 1);
    const std::string utf8 = wide_to_utf8(file);
    return utf8.empty() ? "unknown" : utf8;
}

std::string process_name_from_snapshot(DWORD pid) {
    ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) {
        return "unknown";
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
        return "unknown";
    }
    do {
        if (entry.th32ProcessID == pid) {
            return file_name_from_path(entry.szExeFile);
        }
    } while (Process32NextW(snapshot.get(), &entry));
    return "unknown";
}

std::string process_name_from_pid(DWORD pid) {
    ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (process) {
        wchar_t path[32768];
        DWORD length = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
        if (QueryFullProcessImageNameW(process.get(), 0, path, &length) && length > 0) {
            return file_name_from_path(std::wstring_view(path, length));
        }
    }
    return process_name_from_snapshot(pid);
}

std::string foreground_title(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }
    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), length + 1);
    if (copied <= 0) {
        return {};
    }
    title.resize(static_cast<std::size_t>(copied));
    return wide_to_utf8(title);
}

}  // namespace

ActivityRecord MetricsCollector::collect() const {
    ActivityRecord record;
    record.time = format_local_time(std::time(nullptr));
    record.user_active = user_was_active();
    record.process_name = "unknown";

    const HWND window = GetForegroundWindow();
    if (!window) {
        return record;
    }
    record.window_title = foreground_title(window);

    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != 0) {
        record.process_name = process_name_from_pid(pid);
    }
    return record;
}

std::string MetricsCollector::current_agent_id() {
    wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(buffer, &size) || size == 0) {
        return "unknown-host";
    }
    const std::string name = wide_to_utf8(std::wstring_view(buffer, size));
    return name.empty() ? "unknown-host" : name;
}
