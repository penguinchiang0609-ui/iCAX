#pragma once

#include "CommandTargets/CommandRoute.h"
#include "ProjectExport.h"

#include <cstdint>

namespace iCAX
{
    namespace Project
    {
        inline constexpr const char* kProjectCommandMainName = "Project";
        inline constexpr uint32_t kProjectCommandMainCode = iCAX::Command::CommandHash32(kProjectCommandMainName);

        inline constexpr const char* kProjectGetStateName = "GetState";
        inline constexpr const char* kProjectUndoName = "Undo";
        inline constexpr const char* kProjectRedoName = "Redo";
        inline constexpr const char* kProjectGetUndoRedoStateName = "GetUndoRedoState";

        inline constexpr uint64_t kProjectGetStateCommand = iCAX::Command::MakeCommandCode(kProjectCommandMainName, kProjectGetStateName);
        inline constexpr uint64_t kProjectUndoCommand = iCAX::Command::MakeCommandCode(kProjectCommandMainName, kProjectUndoName);
        inline constexpr uint64_t kProjectRedoCommand = iCAX::Command::MakeCommandCode(kProjectCommandMainName, kProjectRedoName);
        inline constexpr uint64_t kProjectGetUndoRedoStateCommand = iCAX::Command::MakeCommandCode(kProjectCommandMainName, kProjectGetUndoRedoStateName);
    }
}
