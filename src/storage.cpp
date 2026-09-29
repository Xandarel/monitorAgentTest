#include "storage.hpp"

#include "json_util.hpp"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

Storage::Storage(std::filesystem::path path) : path_(std::move(path)) {
    if (path_.empty()) {
        throw StorageError("не указан путь к файлу очереди");
    }
}

std::filesystem::path Storage::temporary_path() const {
    auto temporary = path_;
    temporary += ".tmp";
    return temporary;
}

std::vector<ActivityRecord> Storage::load() const {
    const auto temporary = temporary_path();
    std::error_code error;
    const bool main_exists = std::filesystem::exists(path_, error);
    if (error) {
        throw StorageError("не удалось проверить файл очереди: " + error.message());
    }
    const bool temporary_exists = std::filesystem::exists(temporary, error);
    if (error) {
        throw StorageError("не удалось проверить временный файл очереди: " + error.message());
    }

    // Полный снимок пишется во временный файл и только потом подменяет основной.
    // Если процесс оборвался после записи .tmp, но до замены, на диске остаётся более новый .tmp.
    bool recover_temporary = temporary_exists && !main_exists;
    if (temporary_exists && main_exists) {
        const auto temporary_time = std::filesystem::last_write_time(temporary, error);
        if (error) {
            throw StorageError("не удалось прочитать время временного файла очереди: " + error.message());
        }
        const auto main_time = std::filesystem::last_write_time(path_, error);
        if (error) {
            throw StorageError("не удалось прочитать время файла очереди: " + error.message());
        }
        recover_temporary = temporary_time >= main_time;
    }
    if (recover_temporary) {
        std::filesystem::remove(path_, error);
        error.clear();
        std::filesystem::rename(temporary, path_, error);
        if (error) {
            throw StorageError("не удалось восстановить очередь из временного файла: " + error.message());
        }
    }

    if (!std::filesystem::exists(path_, error) || error) {
        if (error) {
            throw StorageError("не удалось проверить файл очереди: " + error.message());
        }
        return {};
    }

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        throw StorageError("не удалось прочитать файл очереди");
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input && !input.eof()) {
        throw StorageError("ошибка чтения файла очереди");
    }
    std::string text = buffer.str();
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
        return {};
    }
    return parse_storage_json(text);
}

void Storage::save(const std::vector<ActivityRecord>& records) const {
    std::error_code error;
    if (path_.has_parent_path() && !path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path(), error);
        if (error) {
            throw StorageError("не удалось создать каталог очереди: " + error.message());
        }
    }

    const auto temporary = temporary_path();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw StorageError("не удалось открыть временный файл очереди");
        }
        output << make_storage_json(records);
        output.flush();
        if (!output) {
            throw StorageError("не удалось записать очередь на диск");
        }
        output.close();
        if (!output) {
            throw StorageError("не удалось закрыть временный файл очереди");
        }
    }

    std::filesystem::remove(path_, error);
    error.clear();
    std::filesystem::rename(temporary, path_, error);
    if (error) {
        throw StorageError("не удалось заменить файл очереди: " + error.message());
    }
}
