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
                logger::error("Invalid Keywords line in {} (need Keywords|Faction or Keywords|Faction|Rank): {}", path.filename().string(), value);
                continue;
            }

            DisguiseEntry entry;
            entry.keywords = Split(parts[0], ',');
            entry.factionEditorID = parts[1];
            entry.rank = 0;

            if (parts.size() == 3) {
                try {
                    int parsed = std::stoi(parts[2]);
                    if (parsed < -128 || parsed > 127) {
                        logger::error("Rank out of range in {}: {} (using 0)", path.filename().string(), parts[2]);
                        entry.rank = 0;
                    } else {
                        entry.rank = static_cast<std::int8_t>(parsed);
                    }
                } catch (...) {
                    logger::error("Invalid rank in {}: {} (using 0)", path.filename().string(), parts[2]);
                    entry.rank = 0;
                }
            }

            if (entry.keywords.empty()) {
                logger::error("No keywords in line in {}: {}", path.filename().string(), value);
                continue;
            }

            logger::info("  + {} Keywords ({}):", path.filename().string(), entry.keywords.size());
            for (const auto& kw : entry.keywords) {
                logger::info("      - {}", kw);
            }
            logger::info("    Faction: {} | Rank: {}", entry.factionEditorID, entry.rank);

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

            if (key == "EnableMod") {
                std::string lower = value;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                EnableMod = (lower == "true" || lower == "1");
                logger::info("EnableMod = {}", EnableMod);
            } else if (key == "TimeoutDuration") {
                try {
                    TimeoutDuration = std::stof(value);
                    logger::info("TimeoutDuration = {:.1f} real seconds", TimeoutDuration);
                } catch (...) {
                    logger::error("Invalid TimeoutDuration value");
                }
            } else if (key == "FollowerSupport") {
                std::string lower = value;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                FollowerSupport = (lower == "true" || lower == "1");
                logger::info("FollowerSupport = {}", FollowerSupport);
            } else if (key == "DebugMode") {
                std::string lower = value;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                DebugMode = (lower == "true" || lower == "1");
                logger::info("DebugMode = {}", DebugMode);
            }
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
                out << "TimeoutDuration = 5\n";
                out << "FollowerSupport = true\n";
                out << "DebugMode = false\n\n";
                out << "[Disguise]\n";
                out << "; Format: Keyword1,Keyword2|FactionEditorID\n";
                out << "; Format: Keyword1,Keyword2|FactionEditorID|Rank\n";
                out << "; Rank is optional (default 0)\n";
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
            entry.faction = RE::TESForm::LookupByEditorID<RE::TESFaction>(entry.factionEditorID);
            if (!entry.faction) {
                logger::error("Faction not found: {}", entry.factionEditorID);
            } else {
                logger::info("Found faction: {}", entry.factionEditorID);
            }
        }

        logger::info("Form resolution finished.");
    }
}