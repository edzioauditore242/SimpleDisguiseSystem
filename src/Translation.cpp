#include "Translation.h"

#include <fstream>
#include <sstream>

#include "Configuration.h"
#include "Logger.h"

namespace Translation {
    static std::unordered_map<std::string, std::string> strings;

    void Load() {
        strings.clear();

        auto path = std::filesystem::path("Data") / "SKSE" / "Plugins" / "SimpleDisguiseSystem_Translation.txt";

        if (!std::filesystem::exists(path)) {
            logger::info("Translation file not found, using default English");
            return;
        }

        std::ifstream file(path);
        if (!file.is_open()) {
            logger::error("Failed to open translation file");
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            // Remove BOM if present
            if (!line.empty() && static_cast<unsigned char>(line[0]) == 0xEF) {
                line.erase(0, 3);
            }

            // Skip empty lines and comments
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            auto pos = line.find('=');
            if (pos == std::string::npos) continue;

            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

            // Trim
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            if (!key.empty()) {
                strings[key] = value;
            }
        }

        logger::info("Translation loaded ({} strings)", strings.size());
    }

    const char* Get(const std::string& key) {
        auto it = strings.find(key);
        if (it != strings.end()) {
            return it->second.c_str();
        }
        return key.c_str();  // fallback to key itself
    }
}