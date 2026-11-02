#pragma once
#include <Services/IService.h>
#include <GeometryData/GeometryData.h>
#include <optional>

namespace iCAX::GeometryServices
{
    // Geometry only, in the source model frame. The owning product supplies the
    // boundary occurrence and traversal; no scene or process state is retained.
    struct BoundaryLeadAnchor
    {
        GeometryData::Point3 Point;
        GeometryData::Direction3 Tangent, ScrapDirection;
    };
    struct BoundaryLeadInput
    {
        std::uint64_t FaceId=0, WireId=0, EdgeId=0;
        std::size_t CoedgeIndex=0;
        double NativeParameter=0, Length=0, Angle=0, Radius=0;
        bool Reversed=false, Entry=true, Arc=false, FlipSide=false;
        // Optional evaluated offset endpoint in the same model frame. The
        // supporting boundary remains available for local trim validation.
        std::optional<BoundaryLeadAnchor> Anchor;
        // A CAM-authored planar loop replaces the source CAD loop for region
        // checks. The source model remains immutable and supplies only support.
        std::optional<GeometryData::CompositeCurve3> RegionBoundary;
        std::optional<GeometryData::Point3> RegionPoint;
        // Opening leads stay in this particular bounded loop, regardless of
        // retained-side or source face/wire orientation.
        bool RequireInsideWire=false;
        // Explicit bounded-loop exterior check for a retained interior. These
        // region constraints take precedence over face/wire orientation.
        bool RequireOutsideWire=false;
    };
    struct BoundaryLeadGeometry
    {
        GeometryData::CompositeCurve3 Curve;
        std::vector<GeometryData::Point3> Points;
        GeometryData::Point3 Anchor;
        GeometryData::Direction3 Tangent, ScrapDirection;
    };
    struct BoundaryFrame
    {
        GeometryData::Point3 Point;
        GeometryData::Direction3 Tangent,Normal;
    };
    class IBoundaryLeadService : public Services::IService
    {
    public:
        virtual BoundaryFrame Frame(const GeometryData::BRepModel&,const BoundaryLeadInput&) const=0;
        virtual BoundaryLeadGeometry Build(const GeometryData::BRepModel&,const BoundaryLeadInput&) const=0;
        // The batch shares only geometry prepared during this call. Results
        // retain request order and the same support validation as Frame.
        virtual std::vector<BoundaryFrame> Frames(const GeometryData::BRepModel& model,
            const std::vector<BoundaryLeadInput>& inputs) const
        {
            std::vector<BoundaryFrame> frames;frames.reserve(inputs.size());
            for(const auto& input:inputs)frames.push_back(Frame(model,input));
            return frames;
        }
    };
}
