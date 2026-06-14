#include "pch.h"
#include "ITrackCoordinator.h"
#include "TrackCoordinator.h"

//!< 生成Tracker
std::shared_ptr<iCAX::Tracker::ITrackCoordinator> iCAX::Tracker::GenerateTracker(IN std::shared_ptr<iCAX::Database::IRepository> pRepository_)
{
    return std::make_shared<CTrackCoordinator>(pRepository_);
}
