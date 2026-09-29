#pragma once

#include "types.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

class StorageError : public std::runtime_error {
public:
    explicit StorageError(const std::string& message) : std::runtime_error(message) {}
};

class Storage {
public:
    explicit Storage(std::filesystem::path path);

    const std::filesystem::path& path() const { return path_; }

    // Пустой или отсутствующий файл даёт пустую очередь.
    // Повреждённый JSON бросает JsonParseError, ошибка чтения — StorageError.
    std::vector<ActivityRecord> load() const;
    void save(const std::vector<ActivityRecord>& records) const;

private:
    std::filesystem::path temporary_path() const;

    std::filesystem::path path_;
};
