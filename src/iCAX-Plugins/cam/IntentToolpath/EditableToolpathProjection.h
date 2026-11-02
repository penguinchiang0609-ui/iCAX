#pragma once
#include "EditableToolpath.h"

namespace iCAX::CAM::Intent
{
struct SProjectedToolpathSelection final
{
    SToolpathValue Value;
    std::vector<SPathRange> ExcludedRanges;
};
// Re-express a retained selection in an exact upstream parameter domain. Every
// material interval needs one unambiguous exact provenance mapping. Approved
// construction roles may be removed explicitly; point attachments may not drop.
_INTENT_TOOLPATH_EXP SProjectedToolpathSelection ProjectToolpathSelection(
    const SFrozenPathInput& selection,const SFrozenPathInput& upstream,
    IntentId projectionOperation,const std::set<std::string>& removableConstructionRoles,
    const IToolpathGeometryServices&);
_INTENT_TOOLPATH_EXP std::vector<SPathRange> ProjectToolpathRangesToInput(
    const SFrozenPathInput& carrier,const std::vector<SPathRange>&,const SFrozenPathInput& target);
// Remove private pure-evaluation stage addresses after composing exact source
// provenance, and attach common channels to the actual public output port.
_INTENT_TOOLPATH_EXP SToolpathValue FinalizeProjectedToolpath(
    const SFrozenPathInput& finalStage,IntentId publicOperation,
    const std::set<IntentId>& privateOperations,const IToolpathGeometryServices&);
}
