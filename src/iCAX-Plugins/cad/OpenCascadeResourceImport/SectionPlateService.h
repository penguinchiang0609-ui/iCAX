#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"
#include "GeometryData/TubeNeutralGeometry.h"
#include "Services/IService.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include <string>

namespace iCAX::Geometry {
struct SupportStation {double Coordinate=0,From=-1e100,To=1e100;bool AlongX=true,Connection=false;std::size_t LayoutIndex=0;double NominalCoordinate=0;};
// A convex forbidden volume in the same world coordinates as Transform.
// Its convex hull, dilated by Margin, must contain the complete continuous
// sweep supplied by the caller, including any rotational interpolation error.
struct SectionPlateKeepOut {
    std::vector<std::array<double,3>> Vertices;
    double Margin=0;
    bool AffectsBase=true;
};
struct SectionPlateInput {
    std::shared_ptr<const GeometryData::BRepModel> Geometry;
    std::array<double,16> Transform;
    // In the same coordinates as Transform, from the base toward the part.
    // Layout coordinates and the support hull lie in its perpendicular plane.
    std::array<double,3> SupportDirection{0,0,1};
    std::vector<std::uint64_t> FaceIDs;
    std::vector<SupportStation> Stations;
    std::vector<SectionPlateKeepOut> KeepOuts;
    // Pre-clearance for existing bounded opening edges. Exact BRep curve
    // bounds form conservative vertical corridors; no toolpath is required.
    // This does not qualify the clearance of a later oriented cutting beam.
    bool AvoidOpeningBoundaries=false;
    double OpeningBoundaryMargin=1;
    bool AlongX=true;
    bool Automatic=false;
    std::string Pattern="Grid",BaseShape="Rectangle";
    std::string JointMode="Weld";
    double JointClearance=.2,TenonWidth=8,TenonPitch=60,JointWeb=1.5,TenonRadius=1;
    std::string WindowMode="None";
    double WindowWeb=15;
    double AreaWidth=0,AreaDepth=0,AreaX=0,AreaY=0,PostWidth=12;
    double Thickness=3, BaseThickness=5, BaseDrop=40, Margin=15, Gap=.2, MinimumWeb=5;
    // Resource scheduling only; zero uses up to four shared geometry workers.
    std::size_t MaximumConcurrency=0;
    // Geometric layout limit, not a material/load or strength qualification.
    double MaximumSupportSpan=220;
    bool SolidBase=false;
    // Count-driven skeletons retain exactly one station per requested group.
    // Empty position lists use evenly distributed fixed positions.
    bool CountLayout=false;
    std::size_t SupportCount=0,ConnectionCount=0;
    std::vector<double> SupportPositions,ConnectionPositions;
    // Local opening-edge recess depth; zero preserves legacy full corridors.
    double OpeningReliefDepth=0;
};
struct SectionPlateJoint {
    std::string Type;
    std::size_t Index=0,MateStationIndex=0;
    bool MateBase=false;
    std::vector<std::array<double,2>> Boundary;
};
struct SectionPlate {
    // Computational BRep; Build leaves all display triangulations empty.
    // The product may create render data only after Build has completed.
    GeometryData::BRepModel Geometry;
    // Exact local-mm boundaries retained from the validated extrusion cap.
    // Rounded supports expose these without rebuilding an already known prism.
    GeometryData::Tube::CPlanarRegion2 ExactProfile;
    // Terminal preview polylines only. Manufacturing and later edits use
    // ExactProfile; these chords must never feed geometric calculations.
    std::vector<std::array<double,2>> Outline;
    std::vector<std::vector<std::array<double,2>>> Holes;
    // Only automatic ventilation openings are editable; manufacturing and
    // pre-clearance holes are retained as immutable void constraints.
    std::vector<std::size_t> WindowHoleIndices;
    std::vector<SectionPlateJoint> Joints;
    std::array<double,2> Span{};
    std::array<double,16> Frame;
    std::array<double,16> ProjectionFrame{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    double Thickness=0, Distance=0, TopRetreat=0;
    double MeshDeflection=0,ClearanceAllowance=0;
    std::size_t ReliefCount=0;
    std::size_t KeepOutReliefCount=0,RejectedStationCount=0;
    // Whole-workpiece counts are reported on the base. Nonplanar multi-wire
    // faces remain unclassified rather than being guessed to contain holes.
    std::size_t OpeningBoundaryCount=0,UnclassifiedOpeningFaceCount=0;
    // Closed solids use material-side outward normals: upper lips and upper
    // entrances are excluded, while sheet faces have no material-side claim.
    std::size_t OpeningNonExitBoundaryCount=0;
    // Convex-hull upper bound from every positive-weight spline control pole;
    // accepted only at the unchanged 1e-7 mm planarity threshold.
    double OpeningPlaneDeviationUpper=0;
    std::vector<std::array<double,3>> Contacts;
    std::vector<std::array<double,2>> SupportHull;
    std::array<double,3> CenterOfMass{};
    std::string CenterMethod;
    double SupportMargin=0,ContactLength=0;
    bool Supported=false;
    std::size_t StationIndex=0;
    bool Base=false;
    bool Connection=false;
    double MaximumUnsupportedSpan=0,MaximumEndOverhang=0;
};
// BRep plate construction and curve/surface clearance; product owns IDs, recipes,
// resources, confirmation, transactions, and manufacturing qualification.
class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ISectionPlateService : public Services::IService {
public:
    // Editable face suggestions from the lower envelope in Transform's frame.
    // Visibility uses trimmed BRep patches and exact occluding volumes;
    // Build owns full-thickness clearance and actual bearing contacts.
    virtual std::vector<std::uint64_t> FindLowerEnvelopeFaces(const SectionPlateInput&,const std::function<bool()>&)const=0;
    virtual std::vector<SupportStation> SuggestStations(const SectionPlateInput&,double spacing)const=0;
    virtual std::vector<SectionPlate> Build(const SectionPlateInput&,const std::function<bool()>&) const=0;
};
}
