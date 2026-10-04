#pragma once

#include <unordered_map>

#include "Configuration.h"

namespace DisguiseManager {
    struct ActiveDisguise {
        RE::TESFaction* faction = nullptr;
        float removeAtGameTime = -1.0f;  // -1 = no timer
        bool isActive = false;
    };

    inline std::unordered_map<RE::FormID, ActiveDisguise> ActiveDisguises;

    void Evaluate(bool isLoadEvaluation = false);
    void UpdateTimers();
    void OnCombatEnd();
    void Register();
}