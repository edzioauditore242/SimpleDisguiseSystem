#include "DisguiseManager.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <thread>
#include <unordered_map>

#include "Configuration.h"
#include "Logger.h"

namespace DisguiseManager {
    static bool g_playerWasInCombat = false;
    static bool g_pendingPostCombatEvaluate = false;

    // ====================== HELPERS ======================

    static float GetCurrentGameTimeSeconds() {
        auto calendar = RE::Calendar::GetSingleton();
        if (!calendar) return 0.0f;
        return calendar->GetCurrentGameTime() * 24.0f * 3600.0f;
    }

    static float RealSecondsToGameSeconds(float realSeconds) {
        auto calendar = RE::Calendar::GetSingleton();
        float timescale = 20.0f;
        if (calendar) {
            timescale = calendar->GetTimescale();
            if (timescale <= 0.0f) {
                timescale = 20.0f;
            }
        }
        return realSeconds * timescale;
    }

    static std::string GetFactionLabel(RE::TESFaction* faction) {
        if (!faction) return "<null>";

        const char* fullName = faction->GetFullName();
        if (fullName && fullName[0] != '\0') {
            return fullName;
        }

        const char* editorID = faction->GetFormEditorID();
        if (editorID && editorID[0] != '\0') {
            return editorID;
        }

        return std::format("FormID:{:08X}", faction->GetFormID());
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

    static void AddFactionToActor(RE::Actor* actor, RE::TESFaction* faction, std::int8_t rank) {
        if (!actor || !faction) return;

        if (actor->IsInFaction(faction)) {
            // Update rank if different
            auto currentRank = actor->GetFactionRank(faction, false);
            if (currentRank != rank) {
                actor->RemoveFromFaction(faction);
                actor->AddToFaction(faction, rank);
                if (Configuration::DebugMode) {
                    logger::info("Updated faction {} rank {} → {} on {}", GetFactionLabel(faction), currentRank, rank, actor->GetDisplayFullName());
                }
            }
            return;
        }

        actor->AddToFaction(faction, rank);
        if (Configuration::DebugMode) {
            logger::info("Added faction {} (rank {}) to {}", GetFactionLabel(faction), rank, actor->GetDisplayFullName());
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

    static void ApplyToPlayerAndFollowers(RE::TESFaction* faction, bool add, std::int8_t rank = 0) {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !faction) return;

        if (add) {
            AddFactionToActor(player, faction, rank);
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
                            AddFactionToActor(actor.get(), faction, rank);
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
            state.appliedRank = 0;
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

        std::unordered_map<RE::FormID, std::vector<const Configuration::DisguiseEntry*>> factionGroups;

        for (auto& entry : Configuration::DisguiseEntries) {
            if (!entry.faction) continue;
            factionGroups[entry.faction->GetFormID()].push_back(&entry);
        }

        for (auto& [formID, group] : factionGroups) {
            RE::TESFaction* faction = group[0]->faction;
            const std::string& factionEditorID = group[0]->factionEditorID;

            bool conditionMet = false;
            int bestWorn = 0;
            int bestRequired = 0;
            std::int8_t bestRank = -128;  // track highest rank among matching groups
            bool anyMatch = false;

            for (auto* entry : group) {
                const int required = static_cast<int>(entry->keywords.size());
                const int worn = CountWornKeywords(player, entry->keywords);

                if (worn >= required && required > 0) {
                    conditionMet = true;
                    anyMatch = true;
                    bestWorn = worn;
                    bestRequired = required;
                    if (entry->rank > bestRank) {
                        bestRank = entry->rank;
                    }
                } else if (!conditionMet && worn > bestWorn) {
                    bestWorn = worn;
                    bestRequired = required;
                }
            }

            // If condition met but all ranks were default-only path with rank 0
            if (conditionMet && !anyMatch) {
                bestRank = 0;
            }
            if (conditionMet && bestRank == -128) {
                bestRank = 0;
            }

            // Fix: when matching, bestRank starts at -128; if only rank 0 matches, bestRank becomes 0
            if (conditionMet) {
                bool rankFound = false;
                std::int8_t highest = 0;
                bool first = true;
                for (auto* entry : group) {
                    const int required = static_cast<int>(entry->keywords.size());
                    const int worn = CountWornKeywords(player, entry->keywords);
                    if (worn >= required && required > 0) {
                        if (first || entry->rank > highest) {
                            highest = entry->rank;
                            first = false;
                        }
                        rankFound = true;
                    }
                }
                if (rankFound) {
                    bestRank = highest;
                } else {
                    bestRank = 0;
                }
            }

            auto& state = ActiveDisguises[formID];
            state.faction = faction;

            if (Configuration::DebugMode) {
                logger::info("Faction: {} | Best Worn: {}/{} | Condition: {} | Active: {} | Timer: {} | Rank: {}", factionEditorID, bestWorn, bestRequired, conditionMet, state.isActive, state.removeAtGameTime > 0.0f ? "YES" : "no",
                             conditionMet ? bestRank : state.appliedRank);
            }

            if (conditionMet) {
                if (state.removeAtGameTime > 0.0f) {
                    state.removeAtGameTime = -1.0f;
                    if (Configuration::DebugMode) {
                        logger::info("  → Cancelled removal timer");
                    }
                }

                const bool needApply = !state.isActive || !player->IsInFaction(faction) || state.appliedRank != bestRank;

                if (needApply) {
                    ApplyToPlayerAndFollowers(faction, true, bestRank);
                    state.isActive = true;
                    state.appliedRank = bestRank;
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
                    state.appliedRank = 0;
                } else {
                    if (state.isActive && state.removeAtGameTime < 0.0f) {
                        const float gameDuration = RealSecondsToGameSeconds(Configuration::TimeoutDuration);
                        state.removeAtGameTime = now + gameDuration;
                        if (Configuration::DebugMode) {
                            logger::info("  → Started removal timer ({:.0f} real sec ≈ {:.0f} game sec)", Configuration::TimeoutDuration, gameDuration);
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

        if (player) {
            bool isInCombat = player->IsInCombat();

            if (g_pendingPostCombatEvaluate && !isInCombat) {
                g_pendingPostCombatEvaluate = false;
                g_playerWasInCombat = false;

                if (Configuration::DebugMode) {
                    logger::info("Pending post-combat evaluate → re-evaluating disguise");
                }
                OnCombatEnd();
            } else if (g_playerWasInCombat && !isInCombat) {
                static float lastCombatEndTime = -9999.0f;
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
                state.appliedRank = 0;
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
                    state.appliedRank = 0;
                    removedAny = true;
                }
            }

            if (removedAny) {
                g_pendingPostCombatEvaluate = true;

                if (Configuration::DebugMode) {
                    logger::info("Disguise removed by hit → pending re-evaluate when combat ends");
                }

                std::thread([]() {
                    std::this_thread::sleep_for(std::chrono::seconds(4));
                    SKSE::GetTaskInterface()->AddTask([]() {
                        if (!Configuration::EnableMod) return;

                        auto player = RE::PlayerCharacter::GetSingleton();
                        if (!player) return;

                        if (player->IsInCombat()) {
                            if (Configuration::DebugMode) {
                                logger::info("Post-hit check: still in combat → keep waiting for combat end");
                            }
                            return;
                        }

                        g_pendingPostCombatEvaluate = false;
                        if (Configuration::DebugMode) {
                            logger::info("Post-hit check: not in combat → re-evaluating disguise");
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