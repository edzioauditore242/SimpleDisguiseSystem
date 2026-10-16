#pragma once

#include <cstdint>
#include <unordered_map>

#include "Configuration.h"

namespace DisguiseManager {
    struct ActiveDisguise {
        RE::TESFaction* faction = nullptr;
        float removeAtGameTime = -1.0f;
        float armedAtGameTime = -1.0f;
        bool isActive = false;
        std::int8_t appliedRank = 0;
    };

    inline std::unordered_map<RE::FormID, ActiveDisguise> ActiveDisguises;

    void Evaluate(bool isLoadEvaluation = false);
    void UpdateTimers();
    void OnCombatEnd();
    void RemoveAllActiveDisguises();
    void Register();
}