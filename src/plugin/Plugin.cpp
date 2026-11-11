#include "plugin/Camera.h"
#include "plugin/Input.h"
#include "plugin/Menu.h"
#include "plugin/Pose.h"
#include "plugin/Settings.h"
#include "plugin/Skiing.h"
#include "plugin/Skis.h"
#include "plugin/Sounds.h"
#include "plugin/Tdm.h"
#include "plugin/Telemetry.h"

SKSEPluginInfo(
    .Version = REL::Version{ SIS_VERSION_MAJOR, SIS_VERSION_MINOR, SIS_VERSION_PATCH, 0 },
    .Name = "SkiingInSkyrim",
    .Author = "Nick",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

namespace
{
    void OnMessage(SKSE::MessagingInterface::Message* message)
    {
        switch (message->type) {
        case SKSE::MessagingInterface::kPostLoad:
            SkiingInSkyrim::Tdm::Connect();
            SkiingInSkyrim::Camera::Connect();
            break;
        case SKSE::MessagingInterface::kPostPostLoad:
            SkiingInSkyrim::Camera::Install();
            break;
        case SKSE::MessagingInterface::kInputLoaded:
            if (SkiingInSkyrim::Input::Register()) {
                spdlog::info("input sink registered");
            } else {
                spdlog::error("input device manager unavailable; raw input will not be logged");
            }
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            SkiingInSkyrim::Settings::Load();
            SkiingInSkyrim::Skis::Register();
            if (SkiingInSkyrim::Menu::Register()) {
                spdlog::info("SKSE Menu Framework page registered");
            } else {
                spdlog::warn("SKSE Menu Framework not found; settings are only in the INI");
            }
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            SkiingInSkyrim::Skis::Reset();
            SkiingInSkyrim::Skiing::Reset();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            SkiingInSkyrim::Skis::Refresh();
            break;
        case SKSE::MessagingInterface::kNewGame:
            SkiingInSkyrim::Skiing::Reset();
            SkiingInSkyrim::Skis::Refresh();
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse, { .logPattern = "[%T.%e] [%L] %v" });
    spdlog::info("SkiingInSkyrim {} loading on Skyrim {}", SIS_VERSION_STRING,
        REL::Module::get().version().string("."));

    if (REL::Module::IsVR()) {
        spdlog::critical("Skyrim VR is not supported");
        return false;
    }
    if (const auto* messaging = SKSE::GetMessagingInterface();
        messaging == nullptr || !messaging->RegisterListener(OnMessage)) {
        spdlog::critical("SKSE messaging listener registration failed");
        return false;
    }
    SkiingInSkyrim::Telemetry::Install();
    SkiingInSkyrim::Input::Install();
    SkiingInSkyrim::Pose::Install();
    SkiingInSkyrim::Sounds::Install();
    return true;
}
