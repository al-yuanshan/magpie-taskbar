#pragma once
#include <string>

namespace magpie {
std::wstring Wide(const std::string& value);
std::string Utf8(const std::wstring& value);
std::string Get(unsigned short port, const std::string& path);
}
