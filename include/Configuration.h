#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Configuration {
    struct FactionTarget {
        std::string editorID;
        std::int8_t rank = 0;
        RE::TESFaction* faction = nullptr;
    };

    struct DisguiseEntry {
        std::vector<std::string> keywords;
        std::vector<FactionTarget> factions;
    };

    inline bool EnableMod = true;
    inline float TimeoutDuration = 5.0f;
    inline bool FollowerSupport = true;
    inline bool DebugMode = false;

    inline std::vector<DisguiseEntry> DisguiseEntries;
    inline std::filesystem::path IniPath;

    void Load();
    void ResolveForms();
}