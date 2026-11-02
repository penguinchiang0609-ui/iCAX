#include "pch.h"
#include "HealWork.h"

namespace iCAX::ExtrusionRecognition
{
CAD::SCadBRepHealResult HealWork::Run(const GeometryData::BRepModel& geometry,
    const CAD::SCadBRepHealOptions& options,const std::function<bool()>& isCancelled) const
{
    return CAD::CCadBRepHealService{}.Heal(geometry,options,isCancelled);
}
}
