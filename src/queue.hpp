#pragma once

#include "types.hpp"

#include <cstddef>
#include <vector>

inline std::size_t trim_queue(std::vector<ActivityRecord>& records, std::size_t limit) {
    if (records.size() <= limit) {
        return 0;
    }
    const std::size_t dropped = records.size() - limit;
    records.erase(records.begin(), records.begin() + static_cast<std::ptrdiff_t>(dropped));
    return dropped;
}
