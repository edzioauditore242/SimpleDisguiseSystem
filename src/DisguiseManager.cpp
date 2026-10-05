#include "DisguiseManager.h"

#include <chrono>
#include <thread>
#include <unordered_map>
#include <format>
#include "Configuration.h"
#include "Logger.h"

namespace DisguiseManager {
    static bool g_playerWasInCombat = false;

    // ====================== HELPERS ======================

    static float GetCurrentGameTimeSeconds() {
        auto calendar = RE::Calendar::GetSingleton();
        if (!calendar) return 0.0f;
        return calendar->GetCurrentGameTime() * 24.0f * 3600.0f;
    }

    static int CountWornKeywords(RE::Actor* actor, const std::vector<std::string>& required) {
        if (!actor) return 0;

        int count = 0;
        std::vector<bool> found(required.size(), false);

        auto inv = actor->GetInventory([](RE::TESBoundObject& obj) { return obj.IsArmor(); });

        for (auto& [item, data] : inv) {
            if (!data.second->IsWorn()) continue;

            auto armor = item->As<RE::TESObjectARMO>();
            if (!armor) continue;

            for (size_t i = 0; i < required.size(); ++i) {
                if (!found[i] && armor->HasKeywordString(required[i])) {
                    found[i] = true;
                    count++;
                }
            }
        }
        return count;
    }

    static std::string GetFactionLabel(RE::TESFaction* faction) {
        if (!faction) return "<null>";

        // Prefer full name if it exists
        const char* fullName = faction->GetFullName();
        if (fullName && fullName[0] != '\0') {
            return fullName;
        }

        // Fallback to EditorID
        const char* editorID = faction->GetFormEditorID();
        if (editorID && editorID[0] != '\0') {
            return editorID;
        }

        // Last fallback
        return std::format("FormID:{:08X}", faction->GetFormID());
    }

    static void AddFactionToActor(RE::Actor* actor, RE::TESFaction* faction) {
        if (!actor || !faction) return;
        if (!actor->IsInFaction(faction)) {
            actor->AddToFaction(faction, 0);
            if (Configuration::DebugMode) {
                logger::info("Added faction {} to {}", GetFactionLabel(faction), actor->GetDisplayFullName());
            }
        }
    }

    static void RemoveFactionFromActor(RE::Actor* actor, RE::TESFaction* faction) {
        if (!actor || !faction) return;
        if (actor->IsInFaction(faction)) {
            actor->RemoveFromFaction(faction);
            if (Configuration::DebugMode) {
                logger::info("Removed faction {} from {}", GetFactionLabel(faction), actor->GetDisplayFullName());
            }
        }
    }

    static void ApplyToPlayerAndFollowers(RE::TESFaction* faction, bool add) {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !faction) return;

        if (add) {
            AddFactionToActor(player, faction);
        } else {
            RemoveFactionFromActor(player, faction);
        }

        if (Configuration::FollowerSupport) {
            auto processLists = RE::ProcessLists::GetSingleton();
            if (processLists) {
                for (auto& handle : processLists->highActorHandles) {
                    auto actor = handle.get();
                    if (actor && actor->IsPlayerTeammate()) {
                        if (add) {
                            AddFactionToActor(actor.get(), faction);
                        } else {
                            RemoveFactionFromActor(actor.get(), faction);
                        }
                    }
                }
            }
        }
    }

    void RemoveAllActiveDisguises() {
        if (Configuration::DebugMode) {
            logger::info("Removing all active disguise factions (mod disabled or cleanup)");
        }

        for (auto& [formID, state] : ActiveDisguises) {
            if (state.faction && (state.isActive || state.removeAtGameTime > 0.0f)) {
                ApplyToPlayerAndFollowers(state.faction, false);
            }
            state.isActive = false;
            state.removeAtGameTime = -1.0f;
        }
    }

    // ====================== MAIN EVALUATION ======================

    void Evaluate(bool isLoadEvaluation) {
        if (!Configuration::EnableMod) {
            if (Configuration::DebugMode) {
                logger::info("Mod is disabled – skipping evaluation");
            }
            return;
        }

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        if (Configuration::DebugMode) {
            logger::info("=== Running Disguise Evaluation {}===", isLoadEvaluation ? "(LOAD) " : "");
        }

        const float now = GetCurrentGameTimeSeconds();

        // Group entries by faction FormID
        std::unordered_map<RE::FormID, std::vector<const Configuration::DisguiseEntry*>> factionGroups;

        for (auto& entry : Configuration::DisguiseEntries) {
            if (!entry.faction) continue;
            factionGroups[entry.faction->GetFormID()].push_back(&entry);
        }

        for (auto& [formID, group] : factionGroups) {
            RE::TESFaction* faction = group[0]->faction;
            const std::string& factionEditorID = group[0]->factionEditorID;

            // Condition is met if ANY keyword group is fully worn
            bool conditionMet = false;
            int bestWorn = 0;
            int bestRequired = 0;

            for (auto* entry : group) {
                const int required = static_cast<int>(entry->keywords.size());
                const int worn = CountWornKeywords(player, entry->keywords);

                if (worn >= required) {
                    conditionMet = true;
                    bestWorn = worn;
                    bestRequired = required;
                } else if (!conditionMet && worn > bestWorn) {
                    bestWorn = worn;
                    bestRequired = required;
                }
            }

            auto& state = ActiveDisguises[formID];
            state.faction = faction;

            if (Configuration::DebugMode) {
                logger::info("Faction: {} | Best Worn: {}/{} | Condition: {} | Active: {} | Timer: {}", factionEditorID, bestWorn, bestRequired, conditionMet, state.isActive, state.removeAtGameTime > 0.0f ? "YES" : "no");
            }

            if (conditionMet) {
                if (state.removeAtGameTime > 0.0f) {
                    state.removeAtGameTime = -1.0f;
                    if (Configuration::DebugMode) {
                        logger::info("  → Cancelled removal timer");
                    }
                }

                if (!state.isActive || !player->IsInFaction(faction)) {
                    ApplyToPlayerAndFollowers(faction, true);
                    state.isActive = true;
                }
            } else {
                if (isLoadEvaluation) {
                    if (player->IsInFaction(faction)) {
                        if (Configuration::DebugMode) {
                            logger::info("  → Load evaluation: condition not met → removing faction immediately");
                        }
                        ApplyToPlayerAndFollowers(faction, false);
                    }
                    state.isActive = false;
                    state.removeAtGameTime = -1.0f;
                } else {
                    if (state.isActive && state.removeAtGameTime < 0.0f) {
                        state.removeAtGameTime = now + Configuration::TimeoutDuration;
                        if (Configuration::DebugMode) {
                            logger::info("  → Started removal timer ({:.0f} game seconds)", Configuration::TimeoutDuration);
                        }
                    }
                }
            }
        }

        UpdateTimers();

        if (Configuration::DebugMode) {
            logger::info("=== Evaluation finished ===");
        }
    }

    void OnCombatEnd() {
        if (Configuration::DebugMode) {
            logger::info("Combat ended → re-evaluating disguise");
        }
        Evaluate(false);
    }

    // ====================== TIMER + COMBAT CHECK ======================

    void UpdateTimers() {
        if (!Configuration::EnableMod) return;

        const float now = GetCurrentGameTimeSeconds();
        auto player = RE::PlayerCharacter::GetSingleton();

        static float lastCombatEndTime = -9999.0f;

        if (player) {
            bool isInCombat = player->IsInCombat();

            if (g_playerWasInCombat && !isInCombat) {
                if (now - lastCombatEndTime > 3.0f) {
                    lastCombatEndTime = now;
                    g_playerWasInCombat = false;

                    if (Configuration::DebugMode) {
                        logger::info("Player left combat → re-evaluating disguise");
                    }
                    OnCombatEnd();
                }
            } else {
                g_playerWasInCombat = isInCombat;
            }
        }

        for (auto& [formID, state] : ActiveDisguises) {
            if (state.isActive && state.removeAtGameTime > 0.0f && now >= state.removeAtGameTime) {
                if (Configuration::DebugMode) {
                    logger::info("Removal timer expired → removing faction");
                }
                ApplyToPlayerAndFollowers(state.faction, false);
                state.isActive = false;
                state.removeAtGameTime = -1.0f;
            }
        }
    }

    // ====================== EQUIP EVENT ======================

    class EquipEventSink : public RE::BSTEventSink<RE::TESEquipEvent> {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* ev, RE::BSTEventSource<RE::TESEquipEvent>*) override {
            if (!ev || !ev->actor) return RE::BSEventNotifyControl::kContinue;
            if (!Configuration::EnableMod) return RE::BSEventNotifyControl::kContinue;

            auto player = RE::PlayerCharacter::GetSingleton();
            if (ev->actor.get() != player) return RE::BSEventNotifyControl::kContinue;

            auto form = RE::TESForm::LookupByID(ev->baseObject);
            if (!form || !form->IsArmor()) return RE::BSEventNotifyControl::kContinue;

            if (Configuration::DebugMode) {
                logger::info("Armor equip/unequip detected (equipped = {})", ev->equipped);
            }

            SKSE::GetTaskInterface()->AddTask([]() { Evaluate(false); });

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    static EquipEventSink g_equipSink;

    // ====================== HIT EVENT ======================

    class HitEventSink : public RE::BSTEventSink<RE::TESHitEvent> {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* ev, RE::BSTEventSource<RE::TESHitEvent>*) override {
            if (!ev || !ev->cause || !ev->target) return RE::BSEventNotifyControl::kContinue;
            if (!Configuration::EnableMod) return RE::BSEventNotifyControl::kContinue;

            auto player = RE::PlayerCharacter::GetSingleton();
            if (ev->cause.get() != player) return RE::BSEventNotifyControl::kContinue;

            auto target = ev->target->As<RE::Actor>();
            if (!target) return RE::BSEventNotifyControl::kContinue;

            // Ignore dead bodies
            if (target->IsDead()) {
                return RE::BSEventNotifyControl::kContinue;
            }

            bool removedAny = false;

            for (auto& [formID, state] : ActiveDisguises) {
                if (state.isActive && state.faction && target->IsInFaction(state.faction)) {
                    if (Configuration::DebugMode) {
                        logger::info("Player attacked member of active disguise faction → removing disguise immediately");
                    }
                    ApplyToPlayerAndFollowers(state.faction, false);
                    state.isActive = false;
                    state.removeAtGameTime = -1.0f;
                    removedAny = true;
                }
            }

            // Option B: after hit-removal, re-evaluate in ~2 real seconds only if not in combat
            if (removedAny) {
                std::thread([]() {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    SKSE::GetTaskInterface()->AddTask([]() {
                        if (!Configuration::EnableMod) return;

                        auto player = RE::PlayerCharacter::GetSingleton();
                        if (!player) return;

                        if (player->IsInCombat()) {
                            if (Configuration::DebugMode) {
                                logger::info("Post-hit check: player still in combat → skip re-evaluate (wait for combat end)");
                            }
                            return;
                        }

                        if (Configuration::DebugMode) {
                            logger::info("Post-hit check: player not in combat → re-evaluating disguise");
                        }
                        Evaluate(false);
                    });
                }).detach();
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    static HitEventSink g_hitSink;

    // ====================== PERIODIC TIMER CHECK ======================

    static void ScheduleTimerCheck() {
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            SKSE::GetTaskInterface()->AddTask([]() {
                UpdateTimers();
                ScheduleTimerCheck();
            });
        }).detach();
    }

    void Register() {
        auto source = RE::ScriptEventSourceHolder::GetSingleton();
        if (source) {
            source->AddEventSink<RE::TESEquipEvent>(&g_equipSink);
            source->AddEventSink<RE::TESHitEvent>(&g_hitSink);
            logger::info("Equip + Hit event sinks registered");
        }

        ScheduleTimerCheck();
        logger::info("Automatic timer checker started");
        logger::info("DisguiseManager fully registered");
    }
}