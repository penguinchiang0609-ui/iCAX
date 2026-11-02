#pragma once
#include "FiveAxisIntentOperations.h"

namespace iCAX::FiveAxisCAM
{
    FIVE_AXIS_CAM_API iCAX::CAM::Intent::IntentMap BuildFiveAxisIntentPoseField(
        const CFiveAxisIntentOperationContext&,const iCAX::CAM::Intent::SToolpathValue&,
        const iCAX::CAM::Intent::SPortRef&,const std::string&);
    FIVE_AXIS_CAM_API iCAX::CAM::Intent::IntentMap BuildFiveAxisIntentPoseDelta(
        const CFiveAxisIntentOperationContext&,const iCAX::CAM::Intent::SToolpathValue&,
        const iCAX::CAM::Intent::SPortRef&,const iCAX::CAM::Intent::IntentArray&,
        const std::string&);
    // Immutable, source-addressed direction field. The frozen context must
    // outlive the evaluator. Its input value is retained as an owned snapshot.
    // Intrinsic measures use the same neutral native-curve service as the legacy
    // ToolAxisField; no direction is inferred from display samples.
    class FIVE_AXIS_CAM_API CFiveAxisIntentPoseField final
    {
    public:
        CFiveAxisIntentPoseField(const CFiveAxisIntentOperationContext&,
            const iCAX::CAM::Intent::SToolpathValue&,const std::string& controlsSet = {});
        ~CFiveAxisIntentPoseField();
        CFiveAxisIntentPoseField(const CFiveAxisIntentPoseField&) = delete;
        CFiveAxisIntentPoseField& operator=(const CFiveAxisIntentPoseField&) = delete;
        iCAX::GeometryData::Direction3 At(std::size_t segmentIndex,double nativeParameter) const;
        // Local authoring preview of a deletion. Reads the saved expression at
        // one location; does not rebuild/schedule the interpolation field.
        iCAX::GeometryData::Direction3 AtWithoutControls(std::size_t segmentIndex,double nativeParameter,const std::set<std::string>& ids) const;
        iCAX::GeometryData::Direction3 BaseAt(std::size_t segmentIndex,double nativeParameter) const;
        iCAX::GeometryData::Direction3 NormalAt(std::size_t segmentIndex,double nativeParameter) const;
    private:
        friend iCAX::CAM::Intent::IntentMap BuildFiveAxisIntentPoseField(
            const CFiveAxisIntentOperationContext&,const iCAX::CAM::Intent::SToolpathValue&,
            const iCAX::CAM::Intent::SPortRef&,const std::string&);
        friend iCAX::CAM::Intent::IntentMap BuildFiveAxisIntentPoseDelta(
            const CFiveAxisIntentOperationContext&,const iCAX::CAM::Intent::SToolpathValue&,
            const iCAX::CAM::Intent::SPortRef&,const iCAX::CAM::Intent::IntentArray&,
            const std::string&);
        struct Impl;
        std::unique_ptr<const Impl> m_Impl;
    };

    // Called by the registered pose operation on its frozen input. The port is
    // the exact input/source address of the supplied geometry. The resulting
    // common Intervals channel is clipped by the core while its original
    // interpolation domain and neighboring rotations remain unchanged.
    FIVE_AXIS_CAM_API iCAX::CAM::Intent::IntentMap BuildFiveAxisIntentPoseField(
        const CFiveAxisIntentOperationContext&,const iCAX::CAM::Intent::SToolpathValue&,
        const iCAX::CAM::Intent::SPortRef& sourcePort,const std::string& controlsSet = "active");
    FIVE_AXIS_CAM_API iCAX::GeometryData::Direction3 EvaluateFiveAxisIntentPoseField(
        const CFiveAxisIntentOperationContext&,const iCAX::CAM::Intent::SToolpathValue&,
        std::size_t segmentIndex,double nativeParameter,const std::string& controlsSet = {});
    FIVE_AXIS_CAM_API iCAX::CAM::Intent::IntentMap ReverseFiveAxisIntentPoseField(
        const CFiveAxisIntentOperationContext&,const iCAX::CAM::Intent::SToolpathValue&,
        const iCAX::CAM::Intent::SPortRef& sourcePort);
    FIVE_AXIS_CAM_API iCAX::CAM::Intent::IntentMap RemoveFiveAxisIntentPoseControls(
        const CFiveAxisIntentOperationContext&,const iCAX::CAM::Intent::SToolpathValue&,
        const iCAX::CAM::Intent::SPortRef& sourcePort,const std::set<std::string>& ids);
    FIVE_AXIS_CAM_API iCAX::CAM::Intent::IntentMap UpdateFiveAxisIntentPoseFieldPolicy(
        const CFiveAxisIntentOperationContext&,const iCAX::CAM::Intent::SToolpathValue&,
        const iCAX::CAM::Intent::SPortRef& sourcePort,const iCAX::CAM::Intent::IntentMap& policy);
}
