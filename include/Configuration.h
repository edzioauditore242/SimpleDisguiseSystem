#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Configuration {
    struct DisguiseEntry {
        std::vector<std::string> keywords;  // required keywords
        std::string factionEditorID;

        // Runtime
        RE::TESFaction* faction = nullptr;
    };

    // Global settings
    inline float TimeoutDuration = 120.0f;  // in game seconds
    inline bool FollowerSupport = true;
    inline bool DebugMode = false;

    // All loaded entries
    inline std::vector<DisguiseEntry> DisguiseEntries;

    // Path to INI
    inline std::filesystem::path IniPath;

    // Functions
    void Load();
    void ResolveForms();
}