#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"
#include "RigidRegistrationService.h"
#include "Services/IService.h"

namespace iCAX::Geometry
{
    struct SCadReferenceQuery
    {
        enum class Kind { SurfacePoint, Vertex, EdgeMidpoint, CircleCenter, Line, Plane, CylinderAxis, SurfaceNormal } Mode = Kind::SurfacePoint;
        std::uint64_t FaceID = 0;
        RegistrationPoint NearPoint{};
        double MaximumDistanceMm = 2.0;
    };
    struct SCadReferenceResult
    {
        RegistrationPoint Point{}, Direction{};
        std::uint64_t FaceID = 0, EdgeID = 0;
        double DistanceMm = 0;
    };
    struct SCurveReferenceQuery
    {
        enum class Kind { ClosestPoint, Endpoint } Mode = Kind::ClosestPoint;
        RegistrationPoint NearPoint{};
        double MaximumDistanceMm = 2.0;
    };
    struct SCurveReferenceResult
    {
        RegistrationPoint Point{};
        std::size_t SegmentIndex = 0;
        double NativeParameter = 0, DistanceMm = 0;
    };
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ICadReferenceService : public iCAX::Services::IService
    {
    public:
        virtual SCadReferenceResult Resolve(const iCAX::GeometryData::BRepModel&,const SCadReferenceQuery&) const = 0;
        virtual SCurveReferenceResult ResolveCurve(const iCAX::GeometryData::CompositeCurve3&,const SCurveReferenceQuery&) const = 0;
        virtual RegistrationPoint EvaluateCurve(const iCAX::GeometryData::CompositeCurve3&,std::size_t segmentIndex,double nativeParameter) const = 0;
    };
}
