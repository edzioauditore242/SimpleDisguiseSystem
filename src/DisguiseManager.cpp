#include "DisguiseManager.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Configuration.h"
#include "Logger.h"

namespace DisguiseManager {
    static bool g_playerWasInCombat = false;
    static bool g_pendingPostCombatEvaluate = false;

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

    static bool IsWeaponDrawn(RE::Actor* actor) {
        if (!actor) return false;
        if (auto* state = actor->AsActorState()) {
            return state->IsWeaponDrawn();
        }
        return false;
    }

    static int CountWornKeywords(RE::Actor* actor, const std::vector<std::string>& required) {
        if (!actor || required.empty()) return 0;

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

    static RE::Actor* LookupActorByFormID(RE::FormID formID) {
        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) return nullptr;
        return form->As<RE::Actor>();
    }

    static void ApplyAdd(ActiveDisguise& state, RE::TESFaction* faction, std::int8_t rank) {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !faction) return;

        AddFactionToActor(player, faction, rank);
        state.appliedActors.insert(player->GetFormID());

        if (Configuration::FollowerSupport) {
            auto processLists = RE::ProcessLists::GetSingleton();
            if (processLists) {
                for (auto& handle : processLists->highActorHandles) {
                    auto actor = handle.get();
                    if (actor && actor->IsPlayerTeammate()) {
                        AddFactionToActor(actor.get(), faction, rank);
                        state.appliedActors.insert(actor->GetFormID());
                    }
                }
            }
        }
    }

    static void ApplyRemoveTracked(ActiveDisguise& state) {
        if (!state.faction) {
            state.appliedActors.clear();
            return;
        }

        for (auto formID : state.appliedActors) {
            if (auto* actor = LookupActorByFormID(formID)) {
                RemoveFactionFromActor(actor, state.faction);
            }
        }
        state.appliedActors.clear();
    }

    static void CleanupDismissedFollowers(ActiveDisguise& state) {
        if (!state.faction || state.appliedActors.empty()) return;

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        const RE::FormID playerID = player->GetFormID();
        std::vector<RE::FormID> toErase;

        for (auto formID : state.appliedActors) {
            if (formID == playerID) continue;

            auto* actor = LookupActorByFormID(formID);
            if (!actor) {
                toErase.push_back(formID);
                continue;
            }

            if (!actor->IsPlayerTeammate()) {
                RemoveFactionFromActor(actor, state.faction);
                toErase.push_back(formID);
                if (Configuration::DebugMode) {
                    logger::info("Cleaned disguise faction {} from dismissed follower {}", GetFactionLabel(state.faction), actor->GetDisplayFullName());
                }
            }
        }

        for (auto id : toErase) {
            state.appliedActors.erase(id);
        }
    }

    static void SyncCurrentFollowers(ActiveDisguise& state, std::int8_t rank) {
        if (!Configuration::FollowerSupport || !state.isActive || !state.faction) return;

        auto processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return;

        for (auto& handle : processLists->highActorHandles) {
            auto actor = handle.get();
            if (!actor || !actor->IsPlayerTeammate()) continue;

            const auto id = actor->GetFormID();
            if (state.appliedActors.contains(id)) {
                AddFactionToActor(actor.get(), state.faction, rank);
            } else {
                AddFactionToActor(actor.get(), state.faction, rank);
                state.appliedActors.insert(id);
                if (Configuration::DebugMode) {
                    logger::info("Added disguise faction {} to new follower {}", GetFactionLabel(state.faction), actor->GetDisplayFullName());
                }
            }
        }
    }

    static void ClearArmedTimer(ActiveDisguise& state) {
        if (state.armedAtGameTime > 0.0f && Configuration::DebugMode) {
            logger::info("Armed timer cancelled for {}", GetFactionLabel(state.faction));
        }
        state.armedAtGameTime = -1.0f;
    }

    static void RemoveFactionAndLinked(RE::FormID triggerId) {
        std::unordered_set<RE::FormID> toRemove;
        toRemove.insert(triggerId);

        for (auto& entry : Configuration::DisguiseEntries) {
            bool contains = false;
            for (auto& ft : entry.factions) {
                if (ft.faction && ft.faction->GetFormID() == triggerId) {
                    contains = true;
                    break;
                }
            }
            if (!contains) continue;

            for (auto& ft : entry.factions) {
                if (ft.faction) {
                    toRemove.insert(ft.faction->GetFormID());
                }
            }
        }

        for (auto id : toRemove) {
            auto it = ActiveDisguises.find(id);
            if (it == ActiveDisguises.end()) continue;
            auto& state = it->second;
            ApplyRemoveTracked(state);
            state.isActive = false;
            state.removeAtGameTime = -1.0f;
            state.armedAtGameTime = -1.0f;
            state.appliedRank = 0;
        }
    }

    void RemoveAllActiveDisguises() {
        if (Configuration::DebugMode) {
            logger::info("Removing all active disguise factions (mod disabled or cleanup)");
        }

        for (auto& [formID, state] : ActiveDisguises) {
            if (state.faction && (state.isActive || state.removeAtGameTime > 0.0f || state.armedAtGameTime > 0.0f || !state.appliedActors.empty())) {
                ApplyRemoveTracked(state);
            }
            state.isActive = false;
            state.removeAtGameTime = -1.0f;
            state.armedAtGameTime = -1.0f;
            state.appliedRank = 0;
        }
    }

    struct FactionNeed {
        RE::TESFaction* faction = nullptr;
        std::string editorID;
        bool needed = false;
        std::int8_t bestRank = 0;
        int bestWorn = 0;
        int bestRequired = 0;
    };

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

        std::unordered_map<RE::FormID, FactionNeed> needs;

        for (auto& entry : Configuration::DisguiseEntries) {
            for (auto& ft : entry.factions) {
                if (!ft.faction) continue;
                auto id = ft.faction->GetFormID();
                auto& n = needs[id];
                n.faction = ft.faction;
                n.editorID = ft.editorID;
            }
        }

        for (auto& entry : Configuration::DisguiseEntries) {
            if (entry.keywords.empty() || entry.factions.empty()) continue;

            const int required = static_cast<int>(entry.keywords.size());
            const int worn = CountWornKeywords(player, entry.keywords);
            const bool conditionMet = (worn >= required);

            for (auto& ft : entry.factions) {
                if (!ft.faction) continue;
                auto id = ft.faction->GetFormID();
                auto& n = needs[id];
                n.faction = ft.faction;
                n.editorID = ft.editorID;

                if (worn > n.bestWorn) {
                    n.bestWorn = worn;
                    n.bestRequired = required;
                }

                if (conditionMet) {
                    if (!n.needed || ft.rank > n.bestRank) {
                        n.bestRank = ft.rank;
                    }
                    n.needed = true;
                    n.bestWorn = worn;
                    n.bestRequired = required;
                }
            }
        }

        for (auto& [formID, n] : needs) {
            if (!n.faction) continue;

            auto& state = ActiveDisguises[formID];
            state.faction = n.faction;

            if (Configuration::DebugMode) {
                logger::info("Faction: {} | Best Worn: {}/{} | Condition: {} | Active: {} | Timer: {} | Armed: {} | Rank: {}", n.editorID, n.bestWorn, n.bestRequired, n.needed, state.isActive, state.removeAtGameTime > 0.0f ? "YES" : "no",
                             state.armedAtGameTime > 0.0f ? "YES" : "no", n.needed ? n.bestRank : state.appliedRank);
            }

            if (n.needed) {
                if (state.removeAtGameTime > 0.0f) {
                    state.removeAtGameTime = -1.0f;
                    if (Configuration::DebugMode) {
                        logger::info("  → Cancelled removal timer");
                    }
                }

                const bool needApply = !state.isActive || !player->IsInFaction(n.faction) || state.appliedRank != n.bestRank;

                if (needApply) {
                    ApplyAdd(state, n.faction, n.bestRank);
                    state.isActive = true;
                    state.appliedRank = n.bestRank;
                } else {
                    CleanupDismissedFollowers(state);
                    SyncCurrentFollowers(state, n.bestRank);
                }
            } else {
                ClearArmedTimer(state);

                if (isLoadEvaluation) {
                    if (Configuration::DebugMode) {
                        logger::info("  → Load evaluation: condition not met → removing faction from player/followers");
                    }

                    RemoveFactionFromActor(player, n.faction);

                    if (Configuration::FollowerSupport) {
                        auto processLists = RE::ProcessLists::GetSingleton();
                        if (processLists) {
                            for (auto& handle : processLists->highActorHandles) {
                                auto actor = handle.get();
                                if (actor && actor->IsPlayerTeammate()) {
                                    RemoveFactionFromActor(actor.get(), n.faction);
                                }
                            }
                        }
                    }

                    ApplyRemoveTracked(state);

                    state.isActive = false;
                    state.removeAtGameTime = -1.0f;
                    state.armedAtGameTime = -1.0f;
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

    static bool HasArmedWatcher(RE::Actor* player, RE::TESFaction* faction) {
        if (!player || !faction) return false;

        auto processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return false;

        for (auto& handle : processLists->highActorHandles) {
            auto npc = handle.get();
            if (!npc || npc.get() == player) continue;
            if (npc->IsDead()) continue;
            if (npc->IsPlayerTeammate()) continue;
            if (!npc->IsInFaction(faction)) continue;
            if (npc->IsInCombat()) continue;

            bool losExtra = false;
            if (!npc->HasLineOfSight(player, losExtra)) continue;

            return true;
        }
        return false;
    }

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
            if (state.isActive) {
                CleanupDismissedFollowers(state);
                SyncCurrentFollowers(state, state.appliedRank);
            }
        }

        std::vector<RE::FormID> removalExpired;
        for (auto& [formID, state] : ActiveDisguises) {
            if (state.isActive && state.removeAtGameTime > 0.0f && now >= state.removeAtGameTime) {
                removalExpired.push_back(formID);
            }
        }
        for (auto id : removalExpired) {
            if (Configuration::DebugMode) {
                logger::info("Removal timer expired → removing faction (and linked)");
            }
            RemoveFactionAndLinked(id);
        }

        if (!Configuration::EnableArmedTimer || !player) {
            for (auto& [formID, state] : ActiveDisguises) {
                ClearArmedTimer(state);
            }
            return;
        }

        const bool playerInCombat = player->IsInCombat();
        const bool weaponDrawn = IsWeaponDrawn(player);

        std::vector<RE::FormID> armedExpired;

        for (auto& [formID, state] : ActiveDisguises) {
            if (!state.isActive || !state.faction) {
                ClearArmedTimer(state);
                continue;
            }

            if (state.removeAtGameTime > 0.0f) {
                ClearArmedTimer(state);
                continue;
            }

            if (playerInCombat || !weaponDrawn) {
                ClearArmedTimer(state);
                continue;
            }

            const bool watched = HasArmedWatcher(player, state.faction);

            if (watched) {
                if (state.armedAtGameTime < 0.0f) {
                    const float gameDuration = RealSecondsToGameSeconds(Configuration::ArmedTimerDuration);
                    state.armedAtGameTime = now + gameDuration;
                    if (Configuration::DebugMode) {
                        logger::info("Armed timer started for {} ({:.0f} real sec ≈ {:.0f} game sec)", GetFactionLabel(state.faction), Configuration::ArmedTimerDuration, gameDuration);
                    }
                } else if (now >= state.armedAtGameTime) {
                    armedExpired.push_back(formID);
                }
            } else {
                ClearArmedTimer(state);
            }
        }

        for (auto id : armedExpired) {
            if (Configuration::DebugMode) {
                logger::info("Armed timer expired → removing faction (and linked)");
            }
            RemoveFactionAndLinked(id);
        }
    }

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

    class HitEventSink : public RE::BSTEventSink<RE::TESHitEvent> {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* ev, RE::BSTEventSource<RE::TESHitEvent>*) override {
            if (!ev || !ev->cause || !ev->target) return RE::BSEventNotifyControl::kContinue;
            if (!Configuration::EnableMod) return RE::BSEventNotifyControl::kContinue;

            auto player = RE::PlayerCharacter::GetSingleton();
            if (ev->cause.get() != player) return RE::BSEventNotifyControl::kContinue;

            auto target = ev->target->As<RE::Actor>();
            if (!target || target->IsDead()) return RE::BSEventNotifyControl::kContinue;

            std::unordered_set<RE::FormID> toRemove;

            for (auto& entry : Configuration::DisguiseEntries) {
                bool targetInThisEntry = false;
                for (auto& ft : entry.factions) {
                    if (ft.faction && target->IsInFaction(ft.faction)) {
                        targetInThisEntry = true;
                        break;
                    }
                }
                if (!targetInThisEntry) continue;

                for (auto& ft : entry.factions) {
                    if (!ft.faction) continue;
                    auto id = ft.faction->GetFormID();
                    auto it = ActiveDisguises.find(id);
                    if (it != ActiveDisguises.end() && it->second.isActive) {
                        toRemove.insert(id);
                    }
                }
            }

            if (toRemove.empty()) {
                return RE::BSEventNotifyControl::kContinue;
            }

            if (Configuration::DebugMode) {
                logger::info("Player attacked member of active disguise faction → removing linked disguise faction(s)");
            }

            for (auto id : toRemove) {
                auto& state = ActiveDisguises[id];
                ApplyRemoveTracked(state);
                state.isActive = false;
                state.removeAtGameTime = -1.0f;
                state.armedAtGameTime = -1.0f;
                state.appliedRank = 0;
            }

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

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    static HitEventSink g_hitSink;

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