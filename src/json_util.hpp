#pragma once

#include "types.hpp"

#include <ctime>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

class JsonParseError : public std::runtime_error {
public:
    explicit JsonParseError(const std::string& message) : std::runtime_error(message) {}
};

std::string make_storage_json(const std::vector<ActivityRecord>& records);
std::string make_batch_json(std::string_view agent_id,
                            std::time_t timestamp,
                            const std::vector<ActivityRecord>& records);
std::vector<ActivityRecord> parse_storage_json(std::string_view json);

// Пустая строка — проверка пройдена, иначе текст ошибки.
std::string run_self_test();
