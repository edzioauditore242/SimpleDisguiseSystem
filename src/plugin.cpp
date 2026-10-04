#include "Plugin.h"

#include <chrono>
#include <thread>

#include "Configuration.h"
#include "DisguiseManager.h"

static void DelayedEvaluate() {
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(4));
        SKSE::GetTaskInterface()->AddTask([]() {
            logger::info("Running delayed evaluation after load");
            DisguiseManager::Evaluate();
        });
    }).detach();
}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            logger::info("Data loaded – preparing delayed evaluation");
            Configuration::ResolveForms();
            DelayedEvaluate();
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
            logger::info("Save loaded – preparing delayed evaluation");
            // Forms are already resolved, just re-evaluate after a short delay
            DelayedEvaluate();
            break;

        default:
            break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);

    SetupLog();
    logger::info("Simple Disguise System – Rework started");

    Configuration::Load();
    DisguiseManager::Register();
    UI::Register();

    return true;
}