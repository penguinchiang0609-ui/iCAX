#pragma once

#include "CommandHandler/CommandRoute.h"

#include <cstdint>

namespace iCAX::FlatLaserCAM::Commands
{
    inline constexpr const char* KMainName = "FlatLaserCam";
    inline constexpr const char* KGetStateName = "GetState";
    inline constexpr const char* KImportDrawingName = "ImportDrawing";
    inline constexpr const char* KCreateSheetName = "CreateSheet";
    inline constexpr const char* KGenerateNestingName = "GenerateNesting";
    inline constexpr const char* KGenerateToolpathName = "GenerateToolpath";
    inline constexpr const char* KRunSimulationName = "RunSimulation";
    inline constexpr const char* KGenerateNcName = "GenerateNc";

    inline constexpr uint64_t KGetState = iCAX::Command::MakeCommandCode(KMainName, KGetStateName);
    inline constexpr uint64_t KImportDrawing = iCAX::Command::MakeCommandCode(KMainName, KImportDrawingName);
    inline constexpr uint64_t KCreateSheet = iCAX::Command::MakeCommandCode(KMainName, KCreateSheetName);
    inline constexpr uint64_t KGenerateNesting = iCAX::Command::MakeCommandCode(KMainName, KGenerateNestingName);
    inline constexpr uint64_t KGenerateToolpath = iCAX::Command::MakeCommandCode(KMainName, KGenerateToolpathName);
    inline constexpr uint64_t KRunSimulation = iCAX::Command::MakeCommandCode(KMainName, KRunSimulationName);
    inline constexpr uint64_t KGenerateNc = iCAX::Command::MakeCommandCode(KMainName, KGenerateNcName);

    inline const char* GetCommandName(uint32_t nSubCode_)
    {
        switch (nSubCode_)
        {
        case iCAX::Command::CommandHash32(KGetStateName):
            return "FlatLaserCam.GetState";
        case iCAX::Command::CommandHash32(KImportDrawingName):
            return "FlatLaserCam.ImportDrawing";
        case iCAX::Command::CommandHash32(KCreateSheetName):
            return "FlatLaserCam.CreateSheet";
        case iCAX::Command::CommandHash32(KGenerateNestingName):
            return "FlatLaserCam.GenerateNesting";
        case iCAX::Command::CommandHash32(KGenerateToolpathName):
            return "FlatLaserCam.GenerateToolpath";
        case iCAX::Command::CommandHash32(KRunSimulationName):
            return "FlatLaserCam.RunSimulation";
        case iCAX::Command::CommandHash32(KGenerateNcName):
            return "FlatLaserCam.GenerateNc";
        default:
            return "FlatLaserCam.Unknown";
        }
    }
}
