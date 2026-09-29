#include "collector.hpp"
#include "console.hpp"
#include "encoding.hpp"
#include "http_client.hpp"
#include "json_util.hpp"
#include "queue.hpp"
#include "storage.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr int kSampleSec = 5;
constexpr int kFlushSec = 30;
constexpr int kBatchSize = 10;
constexpr std::size_t kMaxRecords = 100;

struct AgentConfig {
    std::string url = "http://localhost:8080/";
    std::string storage_path = "pending_metrics.json";
    std::string agent_id;
    int sample_sec = kSampleSec;
    int flush_sec = kFlushSec;
    int batch_size = kBatchSize;
    bool self_test = false;
    bool help = false;
};

struct PersistOnExit {
    Storage* storage = nullptr;
    std::vector<ActivityRecord>* queue = nullptr;

    ~PersistOnExit() {
        if (!storage || !queue) {
            return;
        }
        try {
            storage->save(*queue);
        } catch (const std::exception& ex) {
            log_line(std::string("не удалось сохранить очередь при выходе: ") + ex.what());
        } catch (...) {
            log_line("не удалось сохранить очередь при выходе");
        }
    }
};

struct ShutdownCompletion {
    ~ShutdownCompletion() { signal_shutdown_complete(); }
};

void print_help() {
    log_line(
        "Использование: monitor_agent [параметры]\n"
        "\n"
        "Консольный агент мониторинга. Каждые 5 секунд фиксирует процесс и заголовок\n"
        "активного окна и факт ввода за последние 5 секунд. Накопленные записи\n"
        "отправляются JSON-пакетом каждые 30 секунд или при достижении 10 записей.\n"
        "Если сервер недоступен, очередь хранится на диске (не больше 100 записей)\n"
        "и отправляется после восстановления связи. При выходе очередь тоже сохраняется.\n"
        "\n"
        "Параметры:\n"
        "  --url URL          Куда отправлять пакеты (по умолчанию http://localhost:8080/)\n"
        "  --storage PATH     Файл очереди (по умолчанию pending_metrics.json)\n"
        "  --agent-id ID      Идентификатор агента (по умолчанию имя компьютера)\n"
        "  --sample-sec N     Интервал сбора в секундах (по умолчанию 5, для отладки)\n"
        "  --flush-sec N      Интервал отправки в секундах (по умолчанию 30, для отладки)\n"
        "  --batch-size N     Сколько записей класть в один пакет (по умолчанию 10)\n"
        "  --self-test        Проверить сериализацию JSON и завершиться\n"
        "  --help             Показать эту справку");
}

std::string require_value(int argc, char** argv, int& index, const char* name) {
    if (index + 1 >= argc) {
        throw std::runtime_error(std::string("для ") + name + " нужно значение");
    }
    ++index;
    return argv[index];
}

int require_int(int argc, char** argv, int& index, const char* name, int min_value, int max_value) {
    const std::string text = require_value(argc, argv, index, name);
    try {
        std::size_t parsed = 0;
        const int value = std::stoi(text, &parsed, 10);
        if (parsed != text.size() || value < min_value || value > max_value) {
            throw std::runtime_error(std::string(name) + " вне диапазона " + std::to_string(min_value) +
                                     ".." + std::to_string(max_value));
        }
        return value;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error(std::string(name) + " должно быть целым числом");
    } catch (const std::out_of_range&) {
        throw std::runtime_error(std::string(name) + " должно быть целым числом");
    }
}

AgentConfig parse_args(int argc, char** argv) {
    AgentConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            config.help = true;
        } else if (arg == "--self-test") {
            config.self_test = true;
        } else if (arg == "--url") {
            config.url = require_value(argc, argv, i, "--url");
        } else if (arg == "--storage") {
            config.storage_path = require_value(argc, argv, i, "--storage");
        } else if (arg == "--agent-id") {
            config.agent_id = require_value(argc, argv, i, "--agent-id");
        } else if (arg == "--sample-sec") {
            config.sample_sec = require_int(argc, argv, i, "--sample-sec", 1, 3600);
        } else if (arg == "--flush-sec") {
            config.flush_sec = require_int(argc, argv, i, "--flush-sec", 1, 86400);
        } else if (arg == "--batch-size") {
            config.batch_size = require_int(argc, argv, i, "--batch-size", 1, static_cast<int>(kMaxRecords));
        } else {
            throw std::runtime_error("неизвестный параметр: " + arg);
        }
    }
    return config;
}

std::string one_line(std::string text) {
    for (char& ch : text) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }
    return text;
}

std::string path_for_log(const std::filesystem::path& path) {
    const std::string text = wide_to_utf8(path.wstring());
    return text.empty() ? path.string() : text;
}

bool send_queue(const std::string& agent_id,
                const HttpClient& http,
                Storage& storage,
                std::vector<ActivityRecord>& queue,
                int batch_size) {
    while (!queue.empty()) {
        const std::size_t count = std::min(queue.size(), static_cast<std::size_t>(batch_size));
        const std::vector<ActivityRecord> batch(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(count));
        const std::string json = make_batch_json(agent_id, std::time(nullptr), batch);
        int status = 0;
        std::string error;
        const SendResult result = http.post_json(json, status, error);
        if (result != SendResult::Delivered) {
            log_line("сервер недоступен (" + error + "), в очереди записей: " + std::to_string(queue.size()));
            storage.save(queue);
            return false;
        }
        queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(count));
        storage.save(queue);
        if (status >= 200 && status < 300) {
            log_line("отправлено записей: " + std::to_string(count) + ", HTTP " + std::to_string(status));
        } else {
            log_line("сервер ответил HTTP " + std::to_string(status) +
                     ", пакет снят с очереди (записей: " + std::to_string(count) + ")");
        }
    }
    return true;
}

int run_agent(const AgentConfig& config) {
    const std::string agent_id = config.agent_id.empty() ? MetricsCollector::current_agent_id() : config.agent_id;
    Storage storage(config.storage_path);
    const HttpClient http(config.url);
    const MetricsCollector collector;

    std::vector<ActivityRecord> queue;
    try {
        queue = storage.load();
    } catch (const JsonParseError& ex) {
        auto damaged = storage.path();
        damaged += ".bad";
        std::error_code error;
        std::filesystem::remove(damaged, error);
        error.clear();
        std::filesystem::rename(storage.path(), damaged, error);
        if (error) {
            throw StorageError(std::string("файл очереди повреждён (") + ex.what() +
                               "), и его не удалось сохранить как .bad: " + error.message());
        }
        log_line(std::string("файл очереди повреждён (") + ex.what() +
                 "). Он переименован в " + path_for_log(damaged) + ", агент продолжает с пустой очередью.");
    }

    PersistOnExit persist{&storage, &queue};
    auto next_flush = std::chrono::steady_clock::now() + std::chrono::seconds(config.flush_sec);
    auto retry_not_before = std::chrono::steady_clock::time_point::min();

    log_line("агент: " + agent_id);
    log_line("endpoint: " + http.url());
    log_line("хранилище: " + path_for_log(std::filesystem::absolute(storage.path())));
    log_line("сбор каждые " + std::to_string(config.sample_sec) +
             " с, отправка каждые " + std::to_string(config.flush_sec) +
             " с или по " + std::to_string(config.batch_size) +
             " записей, лимит очереди: " + std::to_string(kMaxRecords));
    log_line("активность пользователя — ввод за последние 5 секунд. Остановка: Ctrl+C");

    if (!queue.empty()) {
        log_line("на диске уже есть неотправленных записей: " + std::to_string(queue.size()));
        if (!send_queue(agent_id, http, storage, queue, config.batch_size)) {
            retry_not_before = std::chrono::steady_clock::now() + std::chrono::seconds(config.flush_sec);
        }
        next_flush = std::chrono::steady_clock::now() + std::chrono::seconds(config.flush_sec);
    }

    while (wait_for_stop(0)) {
        ActivityRecord record = collector.collect();
        queue.push_back(record);
        const std::size_t dropped = trim_queue(queue, kMaxRecords);
        storage.save(queue);
        if (dropped > 0) {
            log_line("лимит очереди " + std::to_string(kMaxRecords) +
                     ", удалены самые старые записи: " + std::to_string(dropped));
        }
        log_line(record.time + " | " + record.process_name + " | " + one_line(record.window_title) + " | " +
                 (record.user_active ? "активен" : "неактивен") + " | очередь " + std::to_string(queue.size()));

        const auto now = std::chrono::steady_clock::now();
        const bool batch_full = queue.size() >= static_cast<std::size_t>(config.batch_size);
        const bool period_elapsed = now >= next_flush;
        if (!queue.empty() && now >= retry_not_before && (batch_full || period_elapsed)) {
            const bool delivered = send_queue(agent_id, http, storage, queue, config.batch_size);
            const auto after_send = std::chrono::steady_clock::now();
            next_flush = after_send + std::chrono::seconds(config.flush_sec);
            retry_not_before = delivered ? std::chrono::steady_clock::time_point::min()
                                         : after_send + std::chrono::seconds(config.flush_sec);
        }

        if (!wait_for_stop(config.sample_sec * 1000)) {
            break;
        }
    }

    log_line("остановка, неотправленных записей: " + std::to_string(queue.size()));
    if (!queue.empty()) {
        send_queue(agent_id, http, storage, queue, config.batch_size);
    }
    return 0;
}

int run(int argc, char** argv) {
    install_shutdown_handler();
    const ShutdownCompletion shutdown_completion;

    const AgentConfig config = parse_args(argc, argv);
    if (config.help) {
        print_help();
        return 0;
    }
    if (config.self_test) {
        const std::string error = run_self_test();
        if (!error.empty()) {
            std::cerr << error;
            return 2;
        }
        log_line("self-test пройден");
        return 0;
    }
    return run_agent(config);
}

}  // namespace

int main() {
    int argc = 0;
    LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide_argv) {
        return 1;
    }

    std::vector<std::string> args;
    std::vector<char*> argv;
    args.reserve(static_cast<std::size_t>(argc));
    argv.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.push_back(wide_to_utf8(wide_argv[i]));
        argv.push_back(args.back().data());
    }
    LocalFree(wide_argv);

    try {
        return run(argc, argv.data());
    } catch (const std::exception& ex) {
        log_line(std::string("ошибка: ") + ex.what());
        return 1;
    }
}
