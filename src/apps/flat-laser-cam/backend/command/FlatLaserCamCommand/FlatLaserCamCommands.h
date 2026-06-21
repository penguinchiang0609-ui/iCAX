#pragma once

#include <cstdint>

namespace iCAX::FlatLaserCAM::Commands
{
    inline constexpr uint64_t KImportDrawing = 0x464C43414D000001ULL;
    inline constexpr uint64_t KCreateSheet = 0x464C43414D000002ULL;
    inline constexpr uint64_t KGenerateNesting = 0x464C43414D000003ULL;
    inline constexpr uint64_t KGenerateToolpath = 0x464C43414D000004ULL;
    inline constexpr uint64_t KRunSimulation = 0x464C43414D000005ULL;
    inline constexpr uint64_t KGenerateNc = 0x464C43414D000006ULL;

    inline const char* GetCommandName(uint64_t nTypeCode_)
    {
        switch (nTypeCode_)
        {
        case KImportDrawing:
            return "flatLaser.importDrawing";
        case KCreateSheet:
            return "flatLaser.createSheet";
        case KGenerateNesting:
            return "flatLaser.generateNesting";
        case KGenerateToolpath:
            return "flatLaser.generateToolpath";
        case KRunSimulation:
            return "flatLaser.runSimulation";
        case KGenerateNc:
            return "flatLaser.generateNc";
        default:
            return "flatLaser.unknown";
        }
    }
}
