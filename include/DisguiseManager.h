#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "Configuration.h"

namespace DisguiseManager {
    struct ActiveDisguise {
        RE::TESFaction* faction = nullptr;
        float removeAtGameTime = -1.0f;
        float suspicionAtGameTime = -1.0f;  // unified Armed + Sneak
        bool isActive = false;
        std::int8_t appliedRank = 0;

        std::unordered_set<RE::FormID> appliedActors;
    };

    inline std::unordered_map<RE::FormID, ActiveDisguise> ActiveDisguises;

    void Evaluate(bool isLoadEvaluation = false);
    void UpdateTimers();
    void OnCombatEnd();
    void RemoveAllActiveDisguises();
    void Register();
}