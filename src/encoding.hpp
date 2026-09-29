#pragma once

#include <string>
#include <string_view>

std::wstring utf8_to_wide(std::string_view text);
std::string wide_to_utf8(std::wstring_view text);
