#include "DisguiseManager.h"

#include <chrono>
#include <thread>

#include "Configuration.h"
#include "Logger.h"

namespace DisguiseManager {
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

    static void AddFactionToActor(RE::Actor* actor, RE::TESFaction* faction) {
        if (!actor || !faction) return;
        if (!actor->IsInFaction(faction)) {
            actor->AddToFaction(faction, 0);
            if (Configuration::DebugMode) {
                logger::info("Added faction {} to {}", faction->GetFullName(), actor->GetDisplayFullName());
            }
        }
    }

    static void RemoveFactionFromActor(RE::Actor* actor, RE::TESFaction* faction) {
        if (!actor || !faction) return;
        if (actor->IsInFaction(faction)) {
            actor->RemoveFromFaction(faction);
            if (Configuration::DebugMode) {
                logger::info("Removed faction {} from {}", faction->GetFullName(), actor->GetDisplayFullName());
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

    static bool g_playerWasInCombat = false;

    // ====================== MAIN EVALUATION ======================


    void Evaluate(bool isLoadEvaluation) {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        if (Configuration::DebugMode) {
            logger::info("=== Running Disguise Evaluation {}===", isLoadEvaluation ? "(LOAD) " : "");
        }

        const float now = GetCurrentGameTimeSeconds();

        for (auto& entry : Configuration::DisguiseEntries) {
            if (!entry.faction) continue;

            const int required = static_cast<int>(entry.keywords.size());
            const int worn = CountWornKeywords(player, entry.keywords);
            const bool conditionMet = (worn >= required);

            auto& state = ActiveDisguises[entry.faction->GetFormID()];
            state.faction = entry.faction;

            if (Configuration::DebugMode) {
                logger::info("Faction: {} | Worn: {}/{} | Condition: {} | Active: {} | Timer: {}", entry.factionEditorID, worn, required, conditionMet, state.isActive, state.removeAtGameTime > 0.0f ? "YES" : "no");
            }

            if (conditionMet) {
                if (state.removeAtGameTime > 0.0f) {
                    state.removeAtGameTime = -1.0f;
                    if (Configuration::DebugMode) {
                        logger::info("  → Cancelled removal timer");
                    }
                }

                if (!state.isActive || !player->IsInFaction(entry.faction)) {
                    ApplyToPlayerAndFollowers(entry.faction, true);
                    state.isActive = true;
                }
            } else {
                // Condition is NOT met
                if (isLoadEvaluation) {
                    // On load: remove immediately if the player has the faction
                    if (player->IsInFaction(entry.faction)) {
                        if (Configuration::DebugMode) {
                            logger::info("  → Load evaluation: condition not met → removing faction immediately");
                        }
                        ApplyToPlayerAndFollowers(entry.faction, false);
                    }
                    state.isActive = false;
                    state.removeAtGameTime = -1.0f;
                } else {
                    // Normal gameplay: start timer if we previously applied it
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

    void UpdateTimers() {
        const float now = GetCurrentGameTimeSeconds();
        auto player = RE::PlayerCharacter::GetSingleton();

        // ===== Combat End Detection (with anti-loop protection) =====
        static float lastCombatEndTime = -9999.0f;

        if (player) {
            bool isInCombat = player->IsInCombat();

            if (g_playerWasInCombat && !isInCombat) {
                // Only trigger if enough time has passed since the last combat-end
                if (now - lastCombatEndTime > 3.0f) {  // 3 game seconds cooldown
                    lastCombatEndTime = now;
                    g_playerWasInCombat = false;  // set flag FIRST

                    if (Configuration::DebugMode) {
                        logger::info("Player left combat → re-evaluating disguise");
                    }
                    OnCombatEnd();
                }
            } else {
                g_playerWasInCombat = isInCombat;
            }
        }

        // ===== Normal timer expiration =====
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

    void OnCombatEnd() {
        if (Configuration::DebugMode) {
            logger::info("Combat ended → re-evaluating disguise");
        }
        Evaluate();
    }

    // ====================== EQUIP EVENT ======================

    class EquipEventSink : public RE::BSTEventSink<RE::TESEquipEvent> {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* ev, RE::BSTEventSource<RE::TESEquipEvent>*) override {
            if (!ev || !ev->actor) return RE::BSEventNotifyControl::kContinue;

            auto player = RE::PlayerCharacter::GetSingleton();
            if (ev->actor.get() != player) return RE::BSEventNotifyControl::kContinue;

            auto form = RE::TESForm::LookupByID(ev->baseObject);
            if (!form || !form->IsArmor()) return RE::BSEventNotifyControl::kContinue;

            if (Configuration::DebugMode) {
                logger::info("Armor equip/unequip detected (equipped = {})", ev->equipped);
            }

            SKSE::GetTaskInterface()->AddTask([]() { Evaluate(); });

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    static EquipEventSink g_equipSink;

    // ====================== HIT EVENT (attack detection) ======================
    class HitEventSink : public RE::BSTEventSink<RE::TESHitEvent> {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* ev, RE::BSTEventSource<RE::TESHitEvent>*) override {
            if (!ev || !ev->cause || !ev->target) return RE::BSEventNotifyControl::kContinue;

            auto player = RE::PlayerCharacter::GetSingleton();
            if (ev->cause.get() != player) return RE::BSEventNotifyControl::kContinue;

            auto target = ev->target->As<RE::Actor>();
            if (!target) return RE::BSEventNotifyControl::kContinue;

            // Ignore dead bodies
            if (target->IsDead()) {
                return RE::BSEventNotifyControl::kContinue;
            }

            // Check if we are currently disguised as any faction that the target belongs to
            for (auto& [formID, state] : ActiveDisguises) {
                if (state.isActive && state.faction && target->IsInFaction(state.faction)) {
                    if (Configuration::DebugMode) {
                        logger::info("Player attacked member of active disguise faction → removing disguise immediately");
                    }
                    ApplyToPlayerAndFollowers(state.faction, false);
                    state.isActive = false;
                    state.removeAtGameTime = -1.0f;
                }
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
            logger::info("Equip + Combat + Hit event sinks registered");
        }

        ScheduleTimerCheck();
        logger::info("Automatic timer checker started");
        logger::info("DisguiseManager fully registered");
    }
}