#pragma once

#include <string>

struct ActivityRecord {
    std::string time;
    std::string process_name;
    std::string window_title;
    bool user_active = false;
};
