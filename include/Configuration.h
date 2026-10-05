#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Configuration {
    struct DisguiseEntry {
        std::vector<std::string> keywords;
        std::string factionEditorID;

        RE::TESFaction* faction = nullptr;
    };

    inline bool EnableMod = true;
    inline float TimeoutDuration = 120.0f;
    inline bool FollowerSupport = true;
    inline bool DebugMode = false;

    inline std::vector<DisguiseEntry> DisguiseEntries;
    inline std::filesystem::path IniPath;

    void Load();
    void ResolveForms();
}