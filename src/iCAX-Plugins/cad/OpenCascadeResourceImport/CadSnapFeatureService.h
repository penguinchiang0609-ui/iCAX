#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include <array>
#include <string>
#include <vector>

namespace iCAX::Geometry {
    enum class ESnapFeatureKind { Endpoint, Midpoint, CircleCenter, SphereCenter, Edge, Line, Plane, Axis, Center, Direction };
    enum class ESnapConstruction { ThreePointPlane, TwoPointDirection, PlaneU, PlaneV, PlaneCenter, PlaneNormal };
    struct SCadSnapFeature {
        std::string Id;
        ESnapFeatureKind Kind;
        std::uint64_t VertexId=0,EdgeId=0;
        std::vector<std::uint64_t> FaceIds;
        std::vector<std::uint64_t> EdgeIds;
        std::array<double,3> Point{},Direction{};
        std::array<double,3> XDirection{};
        std::string Label;
        bool Derived=false;
        double Radius=0;
        // Picking/display approximation only. Point, Direction and topology IDs
        // above are derived from exact geometry, never from these samples.
        std::vector<std::array<double,3>> EdgeSamples;
        // Analytic bounding references belong to the whole input BRep, not an
        // invented topology face/edge ID. PlaneCorners bounds a finite patch.
        std::string BoxKind;
        std::vector<std::array<double,3>> PlaneCorners;
    };
    struct SCadSnapFeatures {
        std::vector<SCadSnapFeature> Features;
    };
    // Pure BRep analysis. No scene, camera, pixel tolerance, UI preference,
    // alignment rule, persistence or cache lifetime belongs in this service.
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ICadSnapFeatureService : public iCAX::Services::IService {
    public:
        virtual SCadSnapFeatures Analyze(const iCAX::GeometryData::BRepModel&) const=0;
        // Construction consumes already resolved exact features. It is shared
        // by all picking tools and contains no workpiece alignment policy.
        virtual SCadSnapFeature Construct(ESnapConstruction,const std::vector<SCadSnapFeature>&) const=0;
    };
}
