#include "encoding.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <climits>
#include <stdexcept>

namespace {

void ensure_fits_int(std::size_t size) {
    if (size > static_cast<std::size_t>(INT_MAX)) {
        throw std::runtime_error("строка слишком длинная для перекодирования");
    }
}

}  // namespace

std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    ensure_fits_int(text.size());
    const int length = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), length, out.data(), size);
    return out;
}

std::string wide_to_utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    ensure_fits_int(text.size());
    const int length = static_cast<int>(text.size());
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, out.data(), size, nullptr, nullptr);
    return out;
}
