#include "UI.h"

#include <fstream>
#include <sstream>

#include "Configuration.h"
#include "DisguiseManager.h"
#include "Logger.h"

namespace UI {
    // Helper: save current DebugMode back to the INI
    static void SaveDebugModeToIni() {
        if (!std::filesystem::exists(Configuration::IniPath)) {
            logger::error("Cannot save – INI file not found");
            return;
        }

        std::ifstream in(Configuration::IniPath);
        if (!in.is_open()) return;

        std::stringstream buffer;
        std::string line;
        bool found = false;

        while (std::getline(in, line)) {
            if (line.find("DebugMode") != std::string::npos && line.find("=") != std::string::npos) {
                buffer << "DebugMode = " << (Configuration::DebugMode ? "true" : "false") << "\n";
                found = true;
            } else {
                buffer << line << "\n";
            }
        }
        in.close();

        if (!found) {
            buffer << "\n[General]\nDebugMode = " << (Configuration::DebugMode ? "true" : "false") << "\n";
        }

        std::ofstream out(Configuration::IniPath);
        if (out.is_open()) {
            out << buffer.str();
            out.close();
            logger::info("DebugMode saved to INI: {}", Configuration::DebugMode);
        }
    }

    void Register() {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::warn("SKSE Menu Framework is not installed – menu will not be available");
            return;
        }

        SKSEMenuFramework::SetSection("Simple Disguise System");
        SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
        SKSEMenuFramework::AddSectionItem("Debug", RenderDebug);

        logger::info("Menu Framework section registered");
    }

    // ======================== SETTINGS PAGE ========================
    void __stdcall RenderSettings() {
        ImGuiMCP::Text("Simple Disguise System - Settings");
        ImGuiMCP::Separator();
        ImGuiMCP::Text("Entries currently loaded from INI:");
        ImGuiMCP::Spacing();

        if (Configuration::DisguiseEntries.empty()) {
            ImGuiMCP::TextColored(ImGuiMCP::ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No disguise entries loaded.");
        } else {
            for (size_t i = 0; i < Configuration::DisguiseEntries.size(); ++i) {
                auto& entry = Configuration::DisguiseEntries[i];

                ImGuiMCP::Text("Entry #%zu", i + 1);
                ImGuiMCP::BulletText("Keywords (%zu required):", entry.keywords.size());
                for (const auto& kw : entry.keywords) {
                    ImGuiMCP::BulletText("   %s", kw.c_str());
                }
                ImGuiMCP::BulletText("Faction: %s", entry.factionEditorID.c_str());

                if (entry.faction) {
                    ImGuiMCP::TextColored(ImGuiMCP::ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Faction form: OK");
                } else {
                    ImGuiMCP::TextColored(ImGuiMCP::ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Faction form: MISSING!");
                }

                ImGuiMCP::Separator();
            }
        }

        ImGuiMCP::Spacing();
        ImGuiMCP::Separator();
        ImGuiMCP::Spacing();

        if (ImGuiMCP::Button("Reload INI from disk")) {
            logger::info("Reloading INI requested from menu");
            Configuration::Load();
            Configuration::ResolveForms();
            DisguiseManager::Evaluate();
        }

        ImGuiMCP::SameLine();

        if (ImGuiMCP::Button("Force Evaluate Now")) {
            logger::info("Force Evaluate requested from Settings");
            DisguiseManager::Evaluate();
        }
    }

    // ======================== DEBUG PAGE ========================
    void __stdcall RenderDebug() {
        ImGuiMCP::Text("Simple Disguise System - Debug");
        ImGuiMCP::Separator();

        // DebugMode toggle + Save button
        bool debug = Configuration::DebugMode;
        if (ImGuiMCP::Checkbox("Enable Debug Mode (verbose logging)", &debug)) {
            Configuration::DebugMode = debug;
        }

        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Save DebugMode to INI")) {
            SaveDebugModeToIni();
        }

        ImGuiMCP::Spacing();
        ImGuiMCP::Separator();
        ImGuiMCP::Text("Live status:");
        ImGuiMCP::Spacing();

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            ImGuiMCP::Text("Player not available");
            return;
        }

        for (auto& entry : Configuration::DisguiseEntries) {
            if (!entry.faction) continue;

            bool inFaction = player->IsInFaction(entry.faction);

            ImGuiMCP::Text("%s", entry.factionEditorID.c_str());
            ImGuiMCP::BulletText("Currently in faction : %s", inFaction ? "YES" : "no");
            ImGuiMCP::Separator();
        }

        ImGuiMCP::Spacing();
        if (ImGuiMCP::Button("Force Full Evaluation")) {
            logger::info("Force Evaluate requested from Debug page");
            DisguiseManager::Evaluate();
        }
    }
}