#include "FiveAxisIntentGeometry.h"
#include "../../cad/OpenCascadeResourceImport/CadReferenceService.h"
#include <GeometryServices/CurveOffset3DService.h>
#include <GeometryData/BRepPersistence.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace iCAX::FiveAxisCAM
{
    namespace
    {
        using namespace iCAX::CAM::Intent;
        using namespace iCAX::GeometryData;

        void Check(bool valid, const char* message)
        { if (!valid) throw std::invalid_argument(message); }

        ParameterRange Range(const SEditableSegment& segment)
        {
            return std::visit([](const auto& curve) {
                if constexpr (std::is_same_v<std::decay_t<decltype(curve)>, DirectionalCurveOffset3>) return curve.Source.Range;
                else return curve.Range;
            }, segment.Curve);
        }
        ParameterRange Range(const OffsetCurvePiece3& segment)
        {
            return std::visit([](const auto& curve) {
                if constexpr (std::is_same_v<std::decay_t<decltype(curve)>, DirectionalCurveOffset3>) return curve.Source.Range;
                else return curve.Range;
            }, segment.Geometry);
        }
        void ValidateRange(const ParameterRange& range)
        { Check(std::isfinite(range.First) && std::isfinite(range.Last) && range.Last > range.First, "FIVEAXIS_INTENT_NATIVE_RANGE_INVALID"); }
        bool SameCurve(const Curve3& a,const Curve3& b)
        { Persistence::Writer left,right;left.Write(a);right.Write(b);return left.Bytes==right.Bytes; }
        template<class T> bool Finite(const T& value)
        { return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z); }
        double Distance(Point3 a, Point3 b)
        { return std::hypot(a.X-b.X, a.Y-b.Y, a.Z-b.Z); }
        void ValidateIds(const std::vector<std::string>& ids, std::size_t count)
        {
            Check(count>0&&count<=1000000&&ids.size()==count, "FIVEAXIS_INTENT_SEGMENT_IDS_REQUIRED");
            std::set<std::string> seen;
            for(const auto& id:ids)Check(!id.empty()&&seen.insert(id).second, "FIVEAXIS_INTENT_SEGMENT_ID_DUPLICATE");
        }
        void ValidateMappings(const SEditableSegment& segment)
        {
            const auto native=Range(segment);
            for(const auto& source:segment.Sources) {
                Check(!source.Source.OperationId.is_nil()&&!source.Source.PortId.is_nil()&&!source.SourceSegmentId.empty(), "FIVEAXIS_INTENT_MAPPING_SOURCE_INVALID");
                ValidateRange(source.SourceRange); ValidateRange(source.OutputRange);
                Check((source.Precision=="Exact"||source.Precision=="BoundedApproximation")&&std::isfinite(source.ErrorBound)&&source.ErrorBound>=0, "FIVEAXIS_INTENT_MAPPING_PRECISION_INVALID");
                Check(source.OutputRange.First>=native.First-1e-12&&source.OutputRange.Last<=native.Last+1e-12, "FIVEAXIS_INTENT_MAPPING_OUTSIDE_SEGMENT");
            }
        }

        class IntentGeometry final : public IToolpathGeometryServices
        {
            std::shared_ptr<const iCAX::Geometry::ICadReferenceService> m_Cad;
            std::shared_ptr<const iCAX::GeometryServices::ICurveOffset3DService> m_Offset;
            std::shared_ptr<const IToolpathGeometryServices> m_Canonical;
        public:
            IntentGeometry(std::shared_ptr<const iCAX::Geometry::ICadReferenceService> cad,
                std::shared_ptr<const iCAX::GeometryServices::ICurveOffset3DService> offset,
                std::shared_ptr<const IToolpathGeometryServices> canonical)
                :m_Cad(std::move(cad)),m_Offset(std::move(offset)),m_Canonical(canonical?std::move(canonical):CreateToolpathGeometryServices())
            { Check(m_Cad&&m_Offset, "FIVEAXIS_INTENT_BACKEND_REQUIRED"); }

            Point3 Evaluate(const SEditableGeometry& geometry,const SEditableSegment& segment,double parameter) const override
            {
                const auto range=Range(segment); ValidateRange(range);
                Check(std::isfinite(parameter)&&parameter>=range.First&&parameter<=range.Last, "FIVEAXIS_INTENT_PARAMETER_OUTSIDE_RANGE");
                if(segment.Parameterization=="Canonical")return m_Canonical->Evaluate(geometry,segment,parameter);
                Check(segment.Parameterization=="CadNative"&&geometry.Authority==EGeometryAuthority::Spatial3D, "FIVEAXIS_INTENT_PARAMETERIZATION_UNSUPPORTED");
                if(const auto* offset=std::get_if<DirectionalCurveOffset3>(&segment.Curve)) {
                    const auto evaluated=m_Offset->Evaluate(*offset,{parameter});
                    if(evaluated.Status!=ECurveOffsetStatus::SamplesEvaluated||evaluated.Samples.size()!=1)
                        throw std::invalid_argument("FIVEAXIS_INTENT_OFFSET_EVALUATION_FAILED: "+evaluated.Diagnostic);
                    return evaluated.Samples.front().OffsetPoint;
                }
                Check(std::holds_alternative<CurveSegment3>(segment.Curve), "FIVEAXIS_INTENT_AUTHORITY_DIMENSION");
                CompositeCurve3 exact; exact.Segments.push_back(std::get<CurveSegment3>(segment.Curve));
                const auto evaluated=m_Cad->EvaluateCurve(exact,0,parameter);
                return {evaluated[0],evaluated[1],evaluated[2]};
            }

            void Validate(const SEditableGeometry& geometry) const override
            {
                try { m_Canonical->Validate(geometry); return; }
                catch(const std::invalid_argument& error) {
                    const std::string diagnostic=error.what();
                    if(!diagnostic.starts_with("TOOLPATH_UNSUPPORTED_CURVE:")
                        &&diagnostic!="TOOLPATH_UNSUPPORTED_SURFACE_EVALUATOR")throw;
                }
                // The shared validator checks every exact definition before it
                // reports a missing evaluator. Only that capability failure may
                // be completed by this injected native backend.
                Check(geometry.Authority==EGeometryAuthority::Spatial3D, "FIVEAXIS_INTENT_PARAMETERIZATION_UNSUPPORTED");
                for(const auto& segment:geometry.Segments)ValidateMappings(segment);
                std::optional<Point3> previous; Point3 first;
                for(const auto& segment:geometry.Segments) {
                    const auto range=Range(segment);
                    const auto start=Evaluate(geometry,segment,range.Reversed?range.Last:range.First);
                    const auto end=Evaluate(geometry,segment,range.Reversed?range.First:range.Last);
                    Check(Finite(start)&&Finite(end), "FIVEAXIS_INTENT_POINT_INVALID");
                    if(previous)Check(Distance(*previous,start)<=geometry.Tolerance, "FIVEAXIS_INTENT_CHAIN_DISCONNECTED"); else first=start;
                    previous=end;
                }
                if(geometry.Closed)Check(Distance(first,*previous)<=geometry.Tolerance, "FIVEAXIS_INTENT_CLOSURE_INVALID");
            }

            void ValidateManufacturing(const std::string& type,const SOperationEvaluation& input,const SOperationCandidate& result) const override
            {
                // Standard linear leads construct ordinary canonical segments;
                // their material validator only evaluates those lead segments.
                m_Canonical->ValidateManufacturing(type,input,result);
            }
        };
    }

    iCAX::CAM::Intent::SToolpathValue MakeFiveAxisIntentValue(const iCAX::GeometryData::CompositeCurve3& curve,
        const std::vector<std::string>& ids,const std::string& frame,double tolerance,
        const std::vector<std::vector<iCAX::CAM::Intent::SSpanMapping>>& sources)
    {
        ValidateIds(ids,curve.Segments.size());
        Check(!frame.empty()&&std::isfinite(tolerance)&&tolerance>0, "FIVEAXIS_INTENT_FRAME_OR_TOLERANCE_INVALID");
        Check(sources.empty()||sources.size()==curve.Segments.size(), "FIVEAXIS_INTENT_SOURCE_COUNT_MISMATCH");
        SToolpathValue value; value.Geometry.Authority=EGeometryAuthority::Spatial3D;
        value.Geometry.FrameId=frame; value.Geometry.Tolerance=tolerance; value.Geometry.Closed=curve.Closed;
        for(std::size_t i=0;i<curve.Segments.size();++i) {
            SEditableSegment segment; segment.Id=ids[i]; segment.Curve=curve.Segments[i]; segment.Parameterization="CadNative";
            if(!sources.empty())segment.Sources=sources[i]; ValidateRange(Range(segment)); ValidateMappings(segment);
            value.Geometry.Segments.push_back(std::move(segment));
        }
        return value;
    }

    iCAX::CAM::Intent::SToolpathValue MakeFiveAxisOffsetIntentValue(const iCAX::GeometryData::CompositeOffsetCurve3& curve,
        const iCAX::CAM::Intent::SResolvedPath& source,const std::vector<std::string>& ids)
    {
        ValidateIds(ids,curve.Segments.size());
        Check(source.Value.Geometry.Authority==EGeometryAuthority::Spatial3D&&!source.Port.OperationId.is_nil()&&!source.Port.PortId.is_nil(), "FIVEAXIS_INTENT_OFFSET_SOURCE_INVALID");
        SToolpathValue value; value.Geometry=source.Value.Geometry; value.Geometry.Closed=curve.Closed;
        value.Geometry.Meaning="ToolCenter"; value.Geometry.SupportSurface.reset(); value.Geometry.Segments.clear();
        const auto fingerprint=ToolpathFingerprint(source.Value,{"geometry","topology","traversal","frame"});
        for(std::size_t i=0;i<curve.Segments.size();++i) {
            const auto& piece=curve.Segments[i]; const auto range=Range(piece); ValidateRange(range);
            SEditableSegment segment; segment.Id=ids[i]; segment.Parameterization="CadNative";
            std::visit([&](const auto& exact){segment.Curve=exact;},piece.Geometry);
            // Constructed Segment3 connectors use [0,1], while OCCT's native
            // trimmed line uses physical length. A degree-one spline retains
            // the exact connector and its [0,1] addresses without rescaling.
            if(!piece.SourceSegment)if(auto* exact=std::get_if<CurveSegment3>(&segment.Curve))
                if(const auto* line=std::get_if<Segment3>(&exact->Curve)){
                    BSpline3 native;native.Degree=1;native.Poles={line->Start,line->End};native.Knots={0.,1.};native.Multiplicities={2,2};exact->Curve=std::move(native);
                }
            if(piece.SourceSegment) {
                Check(*piece.SourceSegment<source.Value.Geometry.Segments.size(), "FIVEAXIS_INTENT_OFFSET_SOURCE_INDEX_INVALID");
                const auto& original=source.Value.Geometry.Segments[*piece.SourceSegment]; const auto originalRange=Range(original);
                Check(original.Parameterization=="CadNative", "FIVEAXIS_INTENT_OFFSET_SOURCE_PARAMETERIZATION_MISMATCH");
                Check(std::holds_alternative<CurveSegment3>(original.Curve), "FIVEAXIS_INTENT_OFFSET_SOURCE_CURVE_UNSUPPORTED");
                const auto& originalCurve=std::get<CurveSegment3>(original.Curve).Curve;
                const auto& mappedCurve=std::visit([](const auto& exact)->const Curve3& {
                    if constexpr(std::is_same_v<std::decay_t<decltype(exact)>,DirectionalCurveOffset3>)return exact.Source.Curve;
                    else return exact.Curve;
                },piece.Geometry);
                Check(SameCurve(originalCurve,mappedCurve), "FIVEAXIS_INTENT_OFFSET_SOURCE_CURVE_MISMATCH");
                Check(range.First>=originalRange.First&&range.Last<=originalRange.Last, "FIVEAXIS_INTENT_OFFSET_SOURCE_RANGE_INVALID");
                segment.Sources.push_back({source.Port,original.Id,range,range,"Exact",0,0,fingerprint});
            }
            else segment.ConstructionRole="OffsetConnector";
            ValidateMappings(segment); value.Geometry.Segments.push_back(std::move(segment));
        }
        return value;
    }

    std::shared_ptr<const iCAX::CAM::Intent::IToolpathGeometryServices> CreateFiveAxisIntentGeometryServices(
        std::shared_ptr<const iCAX::Geometry::ICadReferenceService> cad,
        std::shared_ptr<const iCAX::GeometryServices::ICurveOffset3DService> offset,
        std::shared_ptr<const iCAX::CAM::Intent::IToolpathGeometryServices> canonical)
    { return std::make_shared<IntentGeometry>(std::move(cad),std::move(offset),std::move(canonical)); }
}
