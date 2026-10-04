#pragma once

#include <string>
#include <unordered_map>

namespace Translation {
    void Load();
    const char* Get(const std::string& key);
}