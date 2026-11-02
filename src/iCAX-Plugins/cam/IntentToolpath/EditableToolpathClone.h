#pragma once
#include "EditableToolpath.h"
namespace iCAX::CAM::Intent::Editable
{
IntentMap PrepareToolpathPlanClone(const IntentMap& snapshot,IntentId command,IntentId destination,
    const std::string& labelScope,const SPlanCloneTransform&,const IToolpathGeometryServices&);
}
