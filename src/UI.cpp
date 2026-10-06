#include "UI.h"

#include <fstream>
#include <sstream>
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
                if (trimmed.find("EnableMod") != std::string::npos) {
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
            }

            buffer << line << "\n";
        }
        in.close();

        if (!enableWritten || !timeoutWritten || !followerWritten || !debugWritten) {
            buffer << "\n[General]\n";
            if (!enableWritten) buffer << "EnableMod = " << (Configuration::EnableMod ? "true" : "false") << "\n";
            if (!timeoutWritten) buffer << "TimeoutDuration = " << Configuration::TimeoutDuration << "\n";
            if (!followerWritten) buffer << "FollowerSupport = " << (Configuration::FollowerSupport ? "true" : "false") << "\n";
            if (!debugWritten) buffer << "DebugMode = " << (Configuration::DebugMode ? "true" : "false") << "\n";
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
        ImGuiMCP::SliderFloat("##TimeoutDuration", &Configuration::TimeoutDuration, 10.0f, 6000.0f, "%.0f");

        ImGuiMCP::Spacing();

        bool follower = Configuration::FollowerSupport;
        if (ImGuiMCP::Checkbox(Translation::Get("Settings_FollowerSupport"), &follower)) {
            Configuration::FollowerSupport = follower;
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
                ImGuiMCP::BulletText("%s: %s", Translation::Get("Settings_Faction"), entry.factionEditorID.c_str());
                ImGuiMCP::BulletText("%s (%zu):", Translation::Get("Settings_Keywords"), entry.keywords.size());
                for (const auto& kw : entry.keywords) {
                    ImGuiMCP::BulletText("   %s", kw.c_str());
                }

                if (entry.faction) {
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

        std::unordered_map<RE::FormID, Configuration::DisguiseEntry*> uniqueFactions;
        for (auto& entry : Configuration::DisguiseEntries) {
            if (entry.faction) {
                uniqueFactions[entry.faction->GetFormID()] = &entry;
            }
        }

        for (auto& [formID, entry] : uniqueFactions) {
            bool inFaction = player->IsInFaction(entry->faction);
            auto it = DisguiseManager::ActiveDisguises.find(formID);
            bool isActive = false;
            bool hasTimer = false;

            if (it != DisguiseManager::ActiveDisguises.end()) {
                isActive = it->second.isActive;
                hasTimer = it->second.removeAtGameTime > 0.0f;
            }

            ImGuiMCP::Text("%s", entry->factionEditorID.c_str());
            ImGuiMCP::BulletText("%s: %s", Translation::Get("Debug_InFaction"), inFaction ? "YES" : "no");
            ImGuiMCP::BulletText("%s: %s", Translation::Get("Debug_Timer"), hasTimer ? "YES" : "no");
            ImGuiMCP::BulletText("%s: %s", Translation::Get("Debug_ModActive"), isActive ? "YES" : "no");
            ImGuiMCP::Separator();
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