#pragma once
#include "FiveAxisIntentOperations.h"
namespace iCAX::FiveAxisCAM
{
// Applied to CAM values only. The CAD resource is never opened for writing.
FIVE_AXIS_CAM_API iCAX::GeometryData::Direction3 CorrectFiveAxisDirection(
    const iCAX::CAM::Intent::SToolpathValue&,iCAX::GeometryData::Direction3,std::size_t segment);
FIVE_AXIS_CAM_API iCAX::GeometryServices::BoundaryFrame CorrectFiveAxisFrame(
    const iCAX::CAM::Intent::SToolpathValue&,std::size_t segment,iCAX::GeometryServices::BoundaryFrame);
FIVE_AXIS_CAM_API std::array<double,16> FiveAxisSegmentCorrection(const iCAX::CAM::Intent::SToolpathValue&,std::size_t);
FIVE_AXIS_CAM_API void TransformFiveAxisIntentPoseFields(const iCAX::CAM::Intent::SToolpathValue& before,
    iCAX::CAM::Intent::SToolpathValue& after,const std::array<double,16>& matrix);
}
