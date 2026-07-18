#pragma once

#include "Facades/FacadeMethod.h"
#include "ProjectExport.h"

#include <cstdint>

namespace iCAX
{
    namespace Project
    {
        inline constexpr const char* kProjectFacadeName = "Project";
        inline constexpr uint32_t kProjectFacadeCode = iCAX::Interaction::InteractionNameHash32(kProjectFacadeName);

        inline constexpr const char* kProjectGetStateName = "GetState";
        inline constexpr const char* kProjectUndoName = "Undo";
        inline constexpr const char* kProjectRedoName = "Redo";
        inline constexpr const char* kProjectGetUndoRedoStateName = "GetUndoRedoState";

        inline constexpr uint64_t kProjectGetStateMethodCode = iCAX::Interaction::MakeFacadeMethodCode(kProjectFacadeName, kProjectGetStateName);
        inline constexpr uint64_t kProjectUndoMethodCode = iCAX::Interaction::MakeFacadeMethodCode(kProjectFacadeName, kProjectUndoName);
        inline constexpr uint64_t kProjectRedoMethodCode = iCAX::Interaction::MakeFacadeMethodCode(kProjectFacadeName, kProjectRedoName);
        inline constexpr uint64_t kProjectGetUndoRedoStateMethodCode = iCAX::Interaction::MakeFacadeMethodCode(kProjectFacadeName, kProjectGetUndoRedoStateName);
    }
}
