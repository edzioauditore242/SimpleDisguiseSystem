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

    void Load() {
        DisguiseEntries.clear();

        IniPath = std::filesystem::path("Data") / "SKSE" / "Plugins" / "SimpleDisguiseSystem.ini";

        if (!std::filesystem::exists(IniPath)) {
            logger::warn("INI not found. Creating default...");

            std::ofstream out(IniPath);
            if (out.is_open()) {
                out << "[General]\n";
                out << "TimeoutDuration = 120\n";
                out << "FollowerSupport = true\n";
                out << "DebugMode = false\n\n";
                out << "[Disguise]\n";
                out << "; Format: Keyword1,Keyword2,Keyword3|FactionEditorID\n";
                out << "Keywords = ArmorMaterialHideCuirass,ArmorMaterialHideBoots,ArmorMaterialHideGauntlets|BanditFaction\n";
                out.close();
            }
            return;
        }

        std::ifstream file(IniPath);
        if (!file.is_open()) {
            logger::error("Failed to open INI");
            return;
        }

        std::string line;
        std::string currentSection;

        while (std::getline(file, line)) {
            if (!line.empty() && static_cast<unsigned char>(line[0]) == 0xEF) {
                line.erase(0, 3);  // remove BOM
            }
            line = Trim(line);

            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line.front() == '[' && line.back() == ']') {
                currentSection = Trim(line.substr(1, line.size() - 2));
                continue;
            }

            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            std::string key = Trim(line.substr(0, eqPos));
            std::string value = Trim(line.substr(eqPos + 1));

            if (currentSection == "General") {
                if (key == "TimeoutDuration") {
                    try {
                        TimeoutDuration = std::stof(value);
                        logger::info("TimeoutDuration = {:.1f} game seconds", TimeoutDuration);
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
            } else if (currentSection == "Disguise") {
                if (key == "Keywords") {
                    auto parts = Split(value, '|');
                    if (parts.size() != 2) {
                        logger::error("Invalid Keywords line (need Keywords|FactionEditorID): {}", value);
                        continue;
                    }

                    DisguiseEntry entry;
                    entry.keywords = Split(parts[0], ',');
                    entry.factionEditorID = parts[1];

                    if (entry.keywords.empty()) {
                        logger::error("No keywords in line: {}", value);
                        continue;
                    }

                    logger::info("Loaded disguise entry:");
                    logger::info("  Keywords ({}):", entry.keywords.size());
                    for (const auto& kw : entry.keywords) {
                        logger::info("    - {}", kw);
                    }
                    logger::info("  Faction: {}", entry.factionEditorID);

                    DisguiseEntries.push_back(std::move(entry));
                }
            }
        }

        file.close();
        logger::info("INI loaded. Total entries: {}", DisguiseEntries.size());
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