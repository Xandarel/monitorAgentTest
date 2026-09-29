#include "http_client.hpp"

#include "encoding.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <stdexcept>

namespace {

class InternetHandle {
public:
    explicit InternetHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~InternetHandle() { reset(); }

    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;

    InternetHandle(InternetHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    InternetHandle& operator=(InternetHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

    void reset() {
        if (handle_) {
            WinHttpCloseHandle(handle_);
            handle_ = nullptr;
        }
    }

private:
    HINTERNET handle_;
};

std::string format_winhttp_error(DWORD code) {
    wchar_t* buffer = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    HMODULE module = nullptr;
    if (code >= 12000 && code <= 12175) {
        flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS;
        module = GetModuleHandleW(L"winhttp.dll");
    }
    const DWORD length = FormatMessageW(flags,
                                        module,
                                        code,
                                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<LPWSTR>(&buffer),
                                        0,
                                        nullptr);
    std::string message = "код " + std::to_string(code);
    if (length != 0 && buffer) {
        message += ": ";
        message += wide_to_utf8(buffer);
        while (!message.empty() &&
               (message.back() == '\n' || message.back() == '\r' || message.back() == ' ')) {
            message.pop_back();
        }
        LocalFree(buffer);
    }
    return message;
}

void drain_response(HINTERNET request) {
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
            return;
        }
        std::string buffer(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read) || read == 0) {
            return;
        }
    }
}

}  // namespace

HttpClient::HttpClient(std::string url) : url_(std::move(url)) {
    const std::string http = "http://";
    const std::string https = "https://";
    std::string rest;
    if (url_.rfind(https, 0) == 0) {
        https_ = true;
        port_ = 443;
        rest = url_.substr(https.size());
    } else if (url_.rfind(http, 0) == 0) {
        https_ = false;
        port_ = 80;
        rest = url_.substr(http.size());
    } else {
        throw std::runtime_error("URL должен начинаться с http:// или https://");
    }

    if (rest.empty()) {
        throw std::runtime_error("в URL не указан хост");
    }
    const auto slash = rest.find('/');
    const std::string host_port = slash == std::string::npos ? rest : rest.substr(0, slash);
    const std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (host_port.empty() || path.empty()) {
        throw std::runtime_error("некорректный URL");
    }
    if (host_port.front() == '[') {
        throw std::runtime_error("IPv6-адреса в URL не поддерживаются");
    }

    std::string host = host_port;
    const auto colon = host_port.rfind(':');
    if (colon != std::string::npos) {
        host = host_port.substr(0, colon);
        const std::string port_text = host_port.substr(colon + 1);
        if (host.empty() || port_text.empty()) {
            throw std::runtime_error("некорректный хост или порт в URL");
        }
        try {
            std::size_t index = 0;
            const int port_value = std::stoi(port_text, &index, 10);
            if (index != port_text.size() || port_value <= 0 || port_value > 65535) {
                throw std::runtime_error("некорректный порт в URL");
            }
            port_ = static_cast<unsigned short>(port_value);
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("некорректный порт в URL");
        } catch (const std::out_of_range&) {
            throw std::runtime_error("некорректный порт в URL");
        }
    }

    host_ = utf8_to_wide(host);
    path_ = utf8_to_wide(path);
    if (host_.empty() || path_.empty()) {
        throw std::runtime_error("не удалось разобрать URL");
    }
}

SendResult HttpClient::post_json(const std::string& body, int& status_code, std::string& error) const {
    status_code = 0;
    error.clear();

    InternetHandle session(WinHttpOpen(L"InsiderMonitorAgent/1.0",
                                       WINHTTP_ACCESS_TYPE_NO_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS,
                                       0));
    if (!session) {
        error = format_winhttp_error(GetLastError());
        return SendResult::Unavailable;
    }
    WinHttpSetTimeouts(session.get(), 2000, 2000, 3000, 3000);

    InternetHandle connection(WinHttpConnect(session.get(), host_.c_str(), port_, 0));
    if (!connection) {
        error = format_winhttp_error(GetLastError());
        return SendResult::Unavailable;
    }

    const DWORD flags = https_ ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request(WinHttpOpenRequest(connection.get(),
                                              L"POST",
                                              path_.c_str(),
                                              nullptr,
                                              WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              flags));
    if (!request) {
        error = format_winhttp_error(GetLastError());
        return SendResult::Unavailable;
    }

    const wchar_t* headers = L"Content-Type: application/json; charset=utf-8\r\n";
    const BOOL sent = WinHttpSendRequest(request.get(),
                                          headers,
                                          static_cast<DWORD>(-1),
                                          body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                                          static_cast<DWORD>(body.size()),
                                          static_cast<DWORD>(body.size()),
                                          0);
    if (!sent || !WinHttpReceiveResponse(request.get(), nullptr)) {
        error = format_winhttp_error(GetLastError());
        return SendResult::Unavailable;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(),
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &status,
                             &status_size,
                             WINHTTP_NO_HEADER_INDEX)) {
        error = format_winhttp_error(GetLastError());
        return SendResult::Unavailable;
    }

    drain_response(request.get());
    status_code = static_cast<int>(status);
    return SendResult::Delivered;
}
