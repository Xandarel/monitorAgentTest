#pragma once

#include "types.hpp"

class MetricsCollector {
public:
    ActivityRecord collect() const;
    static std::string current_agent_id();
};
