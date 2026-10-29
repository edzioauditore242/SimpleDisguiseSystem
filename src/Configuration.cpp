#include "Configuration.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "Logger.h"

namespace Configuration {
    static std::string Trim(std::string str) {
        str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) { return !std::isspace(ch); }));
        str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), str.end());
        return str;
    }

    static std::vector<std::string> Split(const std::string& str, char delimiter) {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delimiter)) {
            item = Trim(item);
            if (!item.empty()) {
                result.push_back(item);
            }
        }
        return result;
    }

    static void LoadDisguiseEntriesFromFile(const std::filesystem::path& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            logger::error("Failed to open disguise INI: {}", path.string());
            return;
        }

        std::string line;
        std::string currentSection;
        int added = 0;

        while (std::getline(file, line)) {
            if (!line.empty() && static_cast<unsigned char>(line[0]) == 0xEF) {
                line.erase(0, 3);
            }
            line = Trim(line);

            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line.front() == '[' && line.back() == ']') {
                currentSection = Trim(line.substr(1, line.size() - 2));
                continue;
            }

            if (currentSection != "Disguise") continue;

            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            std::string key = Trim(line.substr(0, eqPos));
            std::string value = Trim(line.substr(eqPos + 1));

            if (key != "Keywords") continue;

            auto parts = Split(value, '|');
            if (parts.size() != 2 && parts.size() != 3) {
                logger::error("Invalid Keywords line in {} (need Keywords|Faction(s) or Keywords|Faction(s)|Rank): {}", path.filename().string(), value);
                continue;
            }

            DisguiseEntry entry;
            entry.keywords = Split(parts[0], ',');

            if (entry.keywords.empty()) {
                logger::error("No keywords in line in {}: {}", path.filename().string(), value);
                continue;
            }

            std::int8_t sharedRank = 0;
            if (parts.size() == 3) {
                try {
                    int parsed = std::stoi(parts[2]);
                    if (parsed < -128 || parsed > 127) {
                        logger::error("Rank out of range in {}: {} (using 0)", path.filename().string(), parts[2]);
                        sharedRank = 0;
                    } else {
                        sharedRank = static_cast<std::int8_t>(parsed);
                    }
                } catch (...) {
                    logger::error("Invalid rank in {}: {} (using 0)", path.filename().string(), parts[2]);
                    sharedRank = 0;
                }
            }

            auto factionNames = Split(parts[1], ',');
            if (factionNames.empty()) {
                logger::error("No faction in line in {}: {}", path.filename().string(), value);
                continue;
            }

            for (const auto& name : factionNames) {
                FactionTarget ft;
                ft.editorID = name;
                ft.rank = sharedRank;
                entry.factions.push_back(std::move(ft));
            }

            logger::info("  + {} Keywords ({}):", path.filename().string(), entry.keywords.size());
            for (const auto& kw : entry.keywords) {
                logger::info("      - {}", kw);
            }
            logger::info("    Factions ({}):", entry.factions.size());
            for (const auto& ft : entry.factions) {
                logger::info("      - {} (rank {})", ft.editorID, ft.rank);
            }

            DisguiseEntries.push_back(std::move(entry));
            added++;
        }

        if (added > 0) {
            logger::info("Loaded {} disguise entr{} from {}", added, added == 1 ? "y" : "ies", path.filename().string());
        }
    }

    static void LoadGeneralFromMainIni(const std::filesystem::path& path) {
        std::ifstream file(path);
        if (!file.is_open()) return;

        std::string line;
        std::string currentSection;

        // Legacy keys (optional migration)
        bool legacyArmedEnable = false;
        bool legacySneakEnable = false;
        float legacyArmedDur = -1.0f;
        float legacySneakDur = -1.0f;
        bool sawNewSuspicion = false;

        while (std::getline(file, line)) {
            if (!line.empty() && static_cast<unsigned char>(line[0]) == 0xEF) {
                line.erase(0, 3);
            }
            line = Trim(line);

            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line.front() == '[' && line.back() == ']') {
                currentSection = Trim(line.substr(1, line.size() - 2));
                continue;
            }

            if (currentSection != "General") continue;

            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            std::string key = Trim(line.substr(0, eqPos));
            std::string value = Trim(line.substr(eqPos + 1));

            auto toBool = [](std::string v) {
                std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                return v == "true" || v == "1";
            };

            if (key == "EnableMod") {
                EnableMod = toBool(value);
                logger::info("EnableMod = {}", EnableMod);
            } else if (key == "TimeoutDuration") {
                try {
                    TimeoutDuration = std::stof(value);
                    logger::info("TimeoutDuration = {:.1f} real seconds", TimeoutDuration);
                } catch (...) {
                    logger::error("Invalid TimeoutDuration value");
                }
            } else if (key == "FollowerSupport") {
                FollowerSupport = toBool(value);
                logger::info("FollowerSupport = {}", FollowerSupport);
            } else if (key == "DebugMode") {
                DebugMode = toBool(value);
                logger::info("DebugMode = {}", DebugMode);
            }
            // --- New unified suspicion ---
            else if (key == "EnableSuspicionTimer") {
                EnableSuspicionTimer = toBool(value);
                sawNewSuspicion = true;
                logger::info("EnableSuspicionTimer = {}", EnableSuspicionTimer);
            } else if (key == "SuspicionTimer") {
                try {
                    SuspicionTimerDuration = std::stof(value);
                    sawNewSuspicion = true;
                    logger::info("SuspicionTimer = {:.1f} real seconds", SuspicionTimerDuration);
                } catch (...) {
                    logger::error("Invalid SuspicionTimer value");
                }
            } else if (key == "SuspicionWeaponDrawn") {
                SuspicionWeaponDrawn = toBool(value);
                sawNewSuspicion = true;
                logger::info("SuspicionWeaponDrawn = {}", SuspicionWeaponDrawn);
            } else if (key == "SuspicionSneaking") {
                SuspicionSneaking = toBool(value);
                sawNewSuspicion = true;
                logger::info("SuspicionSneaking = {}", SuspicionSneaking);
            }
            // --- Legacy (Armed / Sneak) → migrate if new keys missing ---
            else if (key == "EnableArmedTimer") {
                legacyArmedEnable = toBool(value);
            } else if (key == "ArmedTimer") {
                try {
                    legacyArmedDur = std::stof(value);
                } catch (...) {
                }
            } else if (key == "EnableSneakTimer") {
                legacySneakEnable = toBool(value);
            } else if (key == "SneakTimer") {
                try {
                    legacySneakDur = std::stof(value);
                } catch (...) {
                }
            }
        }

        if (!sawNewSuspicion && (legacyArmedEnable || legacySneakEnable)) {
            EnableSuspicionTimer = true;
            SuspicionWeaponDrawn = legacyArmedEnable;
            SuspicionSneaking = legacySneakEnable;
            float dur = 5.0f;
            if (legacyArmedDur > 0.0f && legacySneakDur > 0.0f) {
                dur = (std::max)(legacyArmedDur, legacySneakDur);
            } else if (legacyArmedDur > 0.0f) {
                dur = legacyArmedDur;
            } else if (legacySneakDur > 0.0f) {
                dur = legacySneakDur;
            }
            SuspicionTimerDuration = dur;
            logger::info("Migrated legacy Armed/Sneak settings → Suspicion timer");
            logger::info("EnableSuspicionTimer = {}", EnableSuspicionTimer);
            logger::info("SuspicionWeaponDrawn = {}", SuspicionWeaponDrawn);
            logger::info("SuspicionSneaking = {}", SuspicionSneaking);
            logger::info("SuspicionTimer = {:.1f}", SuspicionTimerDuration);
        }
    }

    void Load() {
        DisguiseEntries.clear();

        IniPath = std::filesystem::path("Data") / "SKSE" / "Plugins" / "SimpleDisguiseSystem.ini";
        auto customFolder = std::filesystem::path("Data") / "SKSE" / "Plugins" / "SimpleDisguiseSystem";

        if (!std::filesystem::exists(IniPath)) {
            logger::warn("Main INI not found. Creating default...");

            std::ofstream out(IniPath);
            if (out.is_open()) {
                out << "[General]\n";
                out << "EnableMod = true\n";
                out << "TimeoutDuration = 120\n";
                out << "FollowerSupport = true\n";
                out << "DebugMode = false\n";
                out << "EnableSuspicionTimer = false\n";
                out << "SuspicionTimer = 5\n";
                out << "SuspicionWeaponDrawn = true\n";
                out << "SuspicionSneaking = true\n\n";
                out << "[Disguise]\n";
                out << "; Format: Keyword1,Keyword2|FactionEditorID\n";
                out << "; Format: Keyword1,Keyword2|Faction1,Faction2\n";
                out << "; Format: Keyword1,Keyword2|Faction1,Faction2|Rank\n";
                out.close();
            }
        }

        if (!std::filesystem::exists(customFolder)) {
            std::error_code ec;
            std::filesystem::create_directories(customFolder, ec);
            if (!ec) {
                logger::info("Created custom INI folder: {}", customFolder.string());
            }
        }

        if (std::filesystem::exists(IniPath)) {
            logger::info("Loading settings from main INI...");
            LoadGeneralFromMainIni(IniPath);
            logger::info("Loading disguise entries from main INI (if any)...");
            LoadDisguiseEntriesFromFile(IniPath);
        }

        if (std::filesystem::exists(customFolder) && std::filesystem::is_directory(customFolder)) {
            logger::info("Scanning custom INI folder: {}", customFolder.string());

            std::vector<std::filesystem::path> iniFiles;
            for (auto& entry : std::filesystem::directory_iterator(customFolder)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".ini") {
                    iniFiles.push_back(entry.path());
                }
            }

            std::sort(iniFiles.begin(), iniFiles.end());

            for (auto& path : iniFiles) {
                logger::info("Reading custom INI: {}", path.filename().string());
                LoadDisguiseEntriesFromFile(path);
            }
        }

        logger::info("INI load complete. Total disguise entries: {}", DisguiseEntries.size());
    }

    void ResolveForms() {
        logger::info("Resolving factions...");

        for (auto& entry : DisguiseEntries) {
            for (auto& ft : entry.factions) {
                ft.faction = RE::TESForm::LookupByEditorID<RE::TESFaction>(ft.editorID);
                if (!ft.faction) {
                    logger::error("Faction not found: {}", ft.editorID);
                } else {
                    logger::info("Found faction: {}", ft.editorID);
                }
            }
        }

        logger::info("Form resolution finished.");
    }
}