#include "console.hpp"

#include "encoding.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <iostream>
#include <stdexcept>

namespace {

HANDLE g_stop = nullptr;
HANDLE g_done = nullptr;

BOOL WINAPI console_ctrl_handler(DWORD type) {
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_stop) {
            SetEvent(g_stop);
        }
        if (g_done) {
            WaitForSingleObject(g_done, 20000);
        }
        return TRUE;
    default:
        return FALSE;
    }
}

}  // namespace

void log_line(const std::string& text) {
    const std::string line = text + "\n";
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (handle && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
        const std::wstring wide = utf8_to_wide(line);
        DWORD written = 0;
        if (!wide.empty() &&
            WriteConsoleW(handle, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr)) {
            return;
        }
    }
    std::cout << line << std::flush;
}

void install_shutdown_handler() {
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stop || !g_done) {
        throw std::runtime_error("не удалось создать событие остановки");
    }
    if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
        throw std::runtime_error("не удалось зарегистрировать обработчик Ctrl+C");
    }
}

void signal_shutdown_complete() {
    if (g_done) {
        SetEvent(g_done);
    }
}

bool wait_for_stop(int milliseconds) {
    if (!g_stop) {
        return false;
    }
    if (milliseconds < 0) {
        milliseconds = 0;
    }
    const DWORD result = WaitForSingleObject(g_stop, static_cast<DWORD>(milliseconds));
    return result == WAIT_TIMEOUT;
}
