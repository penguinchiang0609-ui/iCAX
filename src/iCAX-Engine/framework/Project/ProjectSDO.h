#pragma once

#include "SDO/SDOMethod.h"
#include "ProjectExport.h"

#include <cstdint>

namespace iCAX
{
    namespace Project
    {
        inline constexpr const char* kProjectSDOName = "Project";
        inline constexpr uint32_t kProjectSDOCode = iCAX::Interaction::InteractionNameHash32(kProjectSDOName);

        inline constexpr const char* kProjectGetStateName = "GetState";
        inline constexpr const char* kProjectUndoName = "Undo";
        inline constexpr const char* kProjectRedoName = "Redo";
        inline constexpr const char* kProjectGetUndoRedoStateName = "GetUndoRedoState";
        inline constexpr const char* kProjectSaveName = "Save";

        inline constexpr uint64_t kProjectGetStateMethodCode = iCAX::Interaction::MakeSDOMethodCode(kProjectSDOName, kProjectGetStateName);
        inline constexpr uint64_t kProjectUndoMethodCode = iCAX::Interaction::MakeSDOMethodCode(kProjectSDOName, kProjectUndoName);
        inline constexpr uint64_t kProjectRedoMethodCode = iCAX::Interaction::MakeSDOMethodCode(kProjectSDOName, kProjectRedoName);
        inline constexpr uint64_t kProjectGetUndoRedoStateMethodCode = iCAX::Interaction::MakeSDOMethodCode(kProjectSDOName, kProjectGetUndoRedoStateName);
        inline constexpr uint64_t kProjectSaveMethodCode = iCAX::Interaction::MakeSDOMethodCode(kProjectSDOName, kProjectSaveName);
    }
}
