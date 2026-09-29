#pragma once

#include <string>

enum class SendResult {
    Delivered,
    Unavailable,
};

class HttpClient {
public:
    explicit HttpClient(std::string url);

    // Delivered — получен любой HTTP-статус (сервер доступен).
    // Unavailable — соединение не установлено или оборвалось.
    SendResult post_json(const std::string& body, int& status_code, std::string& error) const;

    const std::string& url() const { return url_; }

private:
    std::string url_;
    bool https_ = false;
    unsigned short port_ = 80;
    std::wstring host_;
    std::wstring path_;
};
