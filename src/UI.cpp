#include "UI.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "Configuration.h"
#include "DisguiseManager.h"
#include "Logger.h"
#include "Translation.h"

namespace UI {
    static void SaveGeneralSettings() {
        if (!std::filesystem::exists(Configuration::IniPath)) {
            logger::error("Cannot save – INI file not found");
            return;
        }

        std::ifstream in(Configuration::IniPath);
        if (!in.is_open()) return;

        std::stringstream buffer;
        std::string line;
        bool inGeneral = false;
        bool enableWritten = false;
        bool timeoutWritten = false;
        bool followerWritten = false;
        bool debugWritten = false;
        bool susEnableWritten = false;
        bool susTimerWritten = false;
        bool susWeaponWritten = false;
        bool susSneakWritten = false;

        while (std::getline(in, line)) {
            std::string trimmed = line;

            if (trimmed.find("[General]") != std::string::npos) {
                inGeneral = true;
                buffer << line << "\n";
                continue;
            }
            if (trimmed.find("[") != std::string::npos && trimmed.find("]") != std::string::npos) {
                inGeneral = false;
            }

            if (inGeneral) {
                // Skip legacy Armed/Sneak lines (replaced by Suspicion)
                if (trimmed.find("EnableArmedTimer") != std::string::npos || (trimmed.find("ArmedTimer") != std::string::npos && trimmed.find("EnableArmed") == std::string::npos) || trimmed.find("EnableSneakTimer") != std::string::npos ||
                    (trimmed.find("SneakTimer") != std::string::npos && trimmed.find("EnableSneak") == std::string::npos && trimmed.find("Suspicion") == std::string::npos)) {
                    continue;
                }

                if (trimmed.find("EnableMod") != std::string::npos && trimmed.find("EnableSuspicion") == std::string::npos) {
                    buffer << "EnableMod = " << (Configuration::EnableMod ? "true" : "false") << "\n";
                    enableWritten = true;
                    continue;
                }
                if (trimmed.find("TimeoutDuration") != std::string::npos) {
                    buffer << "TimeoutDuration = " << Configuration::TimeoutDuration << "\n";
                    timeoutWritten = true;
                    continue;
                }
                if (trimmed.find("FollowerSupport") != std::string::npos) {
                    buffer << "FollowerSupport = " << (Configuration::FollowerSupport ? "true" : "false") << "\n";
                    followerWritten = true;
                    continue;
                }
                if (trimmed.find("DebugMode") != std::string::npos) {
                    buffer << "DebugMode = " << (Configuration::DebugMode ? "true" : "false") << "\n";
                    debugWritten = true;
                    continue;
                }
                if (trimmed.find("EnableSuspicionTimer") != std::string::npos) {
                    buffer << "EnableSuspicionTimer = " << (Configuration::EnableSuspicionTimer ? "true" : "false") << "\n";
                    susEnableWritten = true;
                    continue;
                }
                if (trimmed.find("SuspicionTimer") != std::string::npos && trimmed.find("EnableSuspicion") == std::string::npos) {
                    buffer << "SuspicionTimer = " << Configuration::SuspicionTimerDuration << "\n";
                    susTimerWritten = true;
                    continue;
                }
                if (trimmed.find("SuspicionWeaponDrawn") != std::string::npos) {
                    buffer << "SuspicionWeaponDrawn = " << (Configuration::SuspicionWeaponDrawn ? "true" : "false") << "\n";
                    susWeaponWritten = true;
                    continue;
                }
                if (trimmed.find("SuspicionSneaking") != std::string::npos) {
                    buffer << "SuspicionSneaking = " << (Configuration::SuspicionSneaking ? "true" : "false") << "\n";
                    susSneakWritten = true;
                    continue;
                }
            }

            buffer << line << "\n";
        }
        in.close();

        if (!enableWritten || !timeoutWritten || !followerWritten || !debugWritten || !susEnableWritten || !susTimerWritten || !susWeaponWritten || !susSneakWritten) {
            buffer << "\n[General]\n";
            if (!enableWritten) buffer << "EnableMod = " << (Configuration::EnableMod ? "true" : "false") << "\n";
            if (!timeoutWritten) buffer << "TimeoutDuration = " << Configuration::TimeoutDuration << "\n";
            if (!followerWritten) buffer << "FollowerSupport = " << (Configuration::FollowerSupport ? "true" : "false") << "\n";
            if (!debugWritten) buffer << "DebugMode = " << (Configuration::DebugMode ? "true" : "false") << "\n";
            if (!susEnableWritten) buffer << "EnableSuspicionTimer = " << (Configuration::EnableSuspicionTimer ? "true" : "false") << "\n";
            if (!susTimerWritten) buffer << "SuspicionTimer = " << Configuration::SuspicionTimerDuration << "\n";
            if (!susWeaponWritten) buffer << "SuspicionWeaponDrawn = " << (Configuration::SuspicionWeaponDrawn ? "true" : "false") << "\n";
            if (!susSneakWritten) buffer << "SuspicionSneaking = " << (Configuration::SuspicionSneaking ? "true" : "false") << "\n";
        }

        std::ofstream out(Configuration::IniPath);
        if (out.is_open()) {
            out << buffer.str();
            out.close();
            logger::info("General settings saved to INI");
        }
    }

    void Register() {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::warn("SKSE Menu Framework is not installed – menu will not be available");
            return;
        }

        Translation::Load();

        SKSEMenuFramework::SetSection("Simple Disguise System");
        SKSEMenuFramework::AddSectionItem(Translation::Get("Menu_Settings"), RenderSettings);
        SKSEMenuFramework::AddSectionItem(Translation::Get("Menu_Debug"), RenderDebug);

        logger::info("Menu Framework section registered");
    }

    void __stdcall RenderSettings() {
        ImGuiMCP::Text("%s", Translation::Get("Settings_Title"));
        ImGuiMCP::Separator();

        bool enable = Configuration::EnableMod;
        if (ImGuiMCP::Checkbox(Translation::Get("Settings_EnableMod"), &enable)) {
            Configuration::EnableMod = enable;
            if (!enable) {
                DisguiseManager::RemoveAllActiveDisguises();
            } else {
                DisguiseManager::Evaluate(false);
            }
        }

        ImGuiMCP::Spacing();

        ImGuiMCP::Text("%s", Translation::Get("Settings_Timeout"));
        ImGuiMCP::SliderFloat("##TimeoutDuration", &Configuration::TimeoutDuration, 1.0f, 600.0f, "%.0f");

        ImGuiMCP::Spacing();

        bool follower = Configuration::FollowerSupport;
        if (ImGuiMCP::Checkbox(Translation::Get("Settings_FollowerSupport"), &follower)) {
            Configuration::FollowerSupport = follower;
        }

        ImGuiMCP::Spacing();
        ImGuiMCP::Separator();
        ImGuiMCP::Spacing();

        ImGuiMCP::Text("%s", Translation::Get("Settings_SuspicionHeader"));

        bool sus = Configuration::EnableSuspicionTimer;
        if (ImGuiMCP::Checkbox(Translation::Get("Settings_EnableSuspicionTimer"), &sus)) {
            Configuration::EnableSuspicionTimer = sus;
            if (!sus) {
                DisguiseManager::UpdateTimers();
            }
        }

        ImGuiMCP::Text("%s", Translation::Get("Settings_SuspicionTimer"));
        ImGuiMCP::SliderFloat("##SuspicionTimer", &Configuration::SuspicionTimerDuration, 1.0f, 60.0f, "%.0f");

        bool weapon = Configuration::SuspicionWeaponDrawn;
        if (ImGuiMCP::Checkbox(Translation::Get("Settings_SuspicionWeaponDrawn"), &weapon)) {
            Configuration::SuspicionWeaponDrawn = weapon;
            DisguiseManager::UpdateTimers();
        }

        bool sneak = Configuration::SuspicionSneaking;
        if (ImGuiMCP::Checkbox(Translation::Get("Settings_SuspicionSneaking"), &sneak)) {
            Configuration::SuspicionSneaking = sneak;
            DisguiseManager::UpdateTimers();
        }

        ImGuiMCP::Spacing();
        ImGuiMCP::Separator();
        ImGuiMCP::Spacing();

        if (ImGuiMCP::Button(Translation::Get("Settings_Save"))) {
            SaveGeneralSettings();
        }

        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(Translation::Get("Settings_ReloadINI"))) {
            logger::info("Reloading INI requested from menu");
            Configuration::Load();
            Configuration::ResolveForms();
            if (Configuration::EnableMod) {
                DisguiseManager::Evaluate();
            } else {
                DisguiseManager::RemoveAllActiveDisguises();
            }
        }

        ImGuiMCP::Spacing();
        ImGuiMCP::Separator();
        ImGuiMCP::Text("%s", Translation::Get("Settings_Entries"));
        ImGuiMCP::Spacing();

        if (Configuration::DisguiseEntries.empty()) {
            ImGuiMCP::TextColored(ImGuiMCP::ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", Translation::Get("Settings_NoEntries"));
        } else {
            for (size_t i = 0; i < Configuration::DisguiseEntries.size(); ++i) {
                auto& entry = Configuration::DisguiseEntries[i];

                ImGuiMCP::Text("%s #%zu", Translation::Get("Settings_Entry"), i + 1);

                std::string factionLine;
                for (size_t f = 0; f < entry.factions.size(); ++f) {
                    if (f > 0) factionLine += ", ";
                    factionLine += entry.factions[f].editorID;
                    factionLine += " (rank ";
                    factionLine += std::to_string(static_cast<int>(entry.factions[f].rank));
                    factionLine += ")";
                }
                ImGuiMCP::BulletText("%s: %s", Translation::Get("Settings_Faction"), factionLine.c_str());

                std::string keywordLine;
                for (size_t k = 0; k < entry.keywords.size(); ++k) {
                    if (k > 0) keywordLine += ", ";
                    keywordLine += entry.keywords[k];
                }
                ImGuiMCP::BulletText("%s: %s", Translation::Get("Settings_Keywords"), keywordLine.c_str());

                bool formsOk = !entry.factions.empty();
                for (auto& ft : entry.factions) {
                    if (!ft.faction) {
                        formsOk = false;
                        break;
                    }
                }

                if (formsOk) {
                    ImGuiMCP::TextColored(ImGuiMCP::ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", Translation::Get("Settings_FormOK"));
                } else {
                    ImGuiMCP::TextColored(ImGuiMCP::ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", Translation::Get("Settings_FormMissing"));
                }

                ImGuiMCP::Separator();
            }
        }
    }

    void __stdcall RenderDebug() {
        ImGuiMCP::Text("%s", Translation::Get("Debug_Title"));
        ImGuiMCP::Separator();

        bool debug = Configuration::DebugMode;
        if (ImGuiMCP::Checkbox(Translation::Get("Debug_Enable"), &debug)) {
            Configuration::DebugMode = debug;
        }

        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(Translation::Get("Debug_Save"))) {
            SaveGeneralSettings();
        }

        ImGuiMCP::Spacing();
        ImGuiMCP::Separator();
        ImGuiMCP::Text("%s", Translation::Get("Debug_LiveStatus"));
        ImGuiMCP::Spacing();

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            ImGuiMCP::Text("%s", Translation::Get("Debug_NoPlayer"));
            return;
        }

        std::unordered_map<RE::FormID, std::string> uniqueFactions;
        for (auto& entry : Configuration::DisguiseEntries) {
            for (auto& ft : entry.factions) {
                if (ft.faction) {
                    uniqueFactions[ft.faction->GetFormID()] = ft.editorID;
                }
            }
        }

        if (uniqueFactions.empty()) {
            ImGuiMCP::Text("%s", Translation::Get("Settings_NoEntries"));
        } else {
            for (auto& [formID, editorID] : uniqueFactions) {
                auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(formID);
                if (!faction) continue;

                bool inFaction = player->IsInFaction(faction);
                auto it = DisguiseManager::ActiveDisguises.find(formID);
                bool isActive = false;
                bool hasTimer = false;
                bool hasSuspicion = false;
                std::int8_t appliedRank = 0;

                if (it != DisguiseManager::ActiveDisguises.end()) {
                    isActive = it->second.isActive;
                    hasTimer = it->second.removeAtGameTime > 0.0f;
                    hasSuspicion = it->second.suspicionAtGameTime > 0.0f;
                    appliedRank = it->second.appliedRank;
                }

                ImGuiMCP::Text("%s", editorID.c_str());
                ImGuiMCP::BulletText("%s: %s", Translation::Get("Debug_InFaction"), inFaction ? "YES" : "no");
                ImGuiMCP::BulletText("%s: %s", Translation::Get("Debug_Timer"), hasTimer ? "YES" : "no");
                ImGuiMCP::BulletText("%s: %d", Translation::Get("Debug_Rank"), static_cast<int>(appliedRank));
                ImGuiMCP::BulletText("%s: %s", Translation::Get("Debug_SuspicionTimer"), hasSuspicion ? "YES" : "no");
                ImGuiMCP::BulletText("%s: %s", Translation::Get("Debug_ModActive"), isActive ? "YES" : "no");
                ImGuiMCP::Separator();
            }
        }

        ImGuiMCP::Spacing();
        if (ImGuiMCP::Button(Translation::Get("Debug_ForceEvaluate"))) {
            logger::info("Force Evaluate requested from Debug page");
            if (Configuration::EnableMod) {
                DisguiseManager::Evaluate();
            }
        }
    }
}