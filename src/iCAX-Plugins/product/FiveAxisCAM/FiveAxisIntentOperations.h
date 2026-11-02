#pragma once
#include "FiveAxisIntentGeometry.h"
#include <GeometryServices/BoundaryLeadService.h>
#include <GeometryServices/CurveOffset3DService.h>
namespace iCAX::Geometry { class ICurveEditingService; }

namespace iCAX::FiveAxisCAM
{
    struct SFiveAxisIntentCadSpan final
    {
        std::uint64_t FaceId=0, WireId=0, EdgeId=0;
        std::size_t CoedgeIndex=0;
    };
    // Seal one source-model version once, then share it across paths and nodes.
    class FIVE_AXIS_CAM_API CFrozenFiveAxisIntentModel final
    {
    public:
        explicit CFrozenFiveAxisIntentModel(iCAX::GeometryData::BRepModel);
        const iCAX::GeometryData::BRepModel& Model() const;
        const std::string& Fingerprint() const;
    private:
        const iCAX::GeometryData::BRepModel m_Model;
        std::string m_Fingerprint;
    };
    // Owns the frozen BRep and binding values. Workers retain no scene callback.
    class FIVE_AXIS_CAM_API CFiveAxisIntentOperationContext final
        : public iCAX::CAM::Intent::IToolpathOperationContext
    {
    public:
        using Bindings=std::map<std::string,SFiveAxisIntentCadSpan>;
        CFiveAxisIntentOperationContext(iCAX::GeometryData::BRepModel,Bindings,
            std::string geometryVersion,std::string serviceVersion,std::string frame,
            int outwardSign,
            std::shared_ptr<const iCAX::GeometryServices::IBoundaryLeadService>,
            std::shared_ptr<const iCAX::GeometryServices::ICurveOffset3DService>,
            std::shared_ptr<const iCAX::Geometry::ICurveEditingService> editing = {});
        CFiveAxisIntentOperationContext(std::shared_ptr<const CFrozenFiveAxisIntentModel>,Bindings,
            std::string geometryVersion,std::string serviceVersion,std::string frame,
            int outwardSign,
            std::shared_ptr<const iCAX::GeometryServices::IBoundaryLeadService>,
            std::shared_ptr<const iCAX::GeometryServices::ICurveOffset3DService>,
            std::shared_ptr<const iCAX::Geometry::ICurveEditingService> editing = {});
        std::string Fingerprint() const override;
        const iCAX::GeometryData::BRepModel& Model() const;
        std::shared_ptr<const CFrozenFiveAxisIntentModel> FrozenModel() const;
        const Bindings& SourceBindings() const;
        const std::string& Frame() const;
        int OutwardSign() const;
        const iCAX::GeometryServices::IBoundaryLeadService& Boundary() const;
        const iCAX::GeometryServices::ICurveOffset3DService& Offset() const;
        std::shared_ptr<const iCAX::Geometry::ICurveEditingService> Editing() const;
    private:
        const std::shared_ptr<const CFrozenFiveAxisIntentModel> m_Model;
        const Bindings m_Bindings;
        const std::string m_GeometryVersion,m_ServiceVersion,m_Frame;
        const int m_OutwardSign;
        const std::shared_ptr<const iCAX::GeometryServices::IBoundaryLeadService> m_Boundary;
        const std::shared_ptr<const iCAX::GeometryServices::ICurveOffset3DService> m_Offset;
        const std::shared_ptr<const iCAX::Geometry::ICurveEditingService> m_Editing;
        std::string m_Fingerprint;
    };
    using FiveAxisIntentContextResolver=std::function<std::shared_ptr<const CFiveAxisIntentOperationContext>(
        const iCAX::CAM::Intent::IntentMap& persistentContext)>;
    FIVE_AXIS_CAM_API CFiveAxisIntentOperationContext::Bindings BuildFiveAxisIntentCadBindings(
        const iCAX::CAM::Intent::IntentMap& authoritativeSourceSpans);

    // Separately editable pose, kerf, trace and lead operations. Sources supply
    // the common fiveaxis.intent/1 product metadata channel. Authored
    // keypoint fields and unsupported legacy process branches fail explicitly.
    FIVE_AXIS_CAM_API void RegisterFiveAxisIntentOperations(
        iCAX::CAM::Intent::CToolpathOperationRegistry&,FiveAxisIntentContextResolver);
    FIVE_AXIS_CAM_API iCAX::GeometryData::Direction3 EvaluateFiveAxisIntentDirection(
        const CFiveAxisIntentOperationContext&,const iCAX::CAM::Intent::SToolpathValue&,
        std::size_t segmentIndex,double nativeParameter);
}
