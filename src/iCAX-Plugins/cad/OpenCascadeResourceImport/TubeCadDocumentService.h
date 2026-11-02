#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/TubeNeutralGeometry.h"
#include "Services/IService.h"
#include <map>
#include <functional>
namespace iCAX::CAD
{
    enum class ETubeSectionDimension { Radius, Width, Height };
    // An editor projection addresses the original geometric node. It is never
    // persisted as an independent 2D recipe and contains exact curves.
    struct STubeCadSketchView final
    {
        std::string NodeID;
        std::uint64_t Revision=0;
        std::variant<GeometryData::Tube::CPlanarRegion2,
            GeometryData::Tube::SOpeningProfileField> Geometry;
        std::optional<GeometryData::Placement3> ConstructionFrame;
        std::optional<GeometryData::Tube::SBoundarySurfaceRef> SurfaceSupport;
    };
    struct STubeCadEvaluation final
    {
        GeometryData::BRepModel Shape;
        std::map<std::string, GeometryData::BRepModel> ConstructionBodies;
        std::vector<std::string> Diagnostics;
        std::vector<std::string> UnavailableConstructionIDs;
    };
    struct STubeCadEditResult final
    {
        GeometryData::Tube::CTubeCadDocument Document;
        STubeCadEvaluation Evaluation;
    };
    // The neutral document is authoritative. This service has no scene,
    // product, resource publication, file ownership or editor-specific state.
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ITubeCadDocumentService : public Services::IService
    {
    public:
        virtual ~ITubeCadDocumentService()=default;
        // Optional caller-owned telemetry, in milliseconds; geometry policy is unchanged.
        virtual STubeCadEditResult Import(const GeometryData::BRepModel& Source,
            const std::function<void(const std::string&, double)>& StageCompleted = {}) const=0;
        virtual STubeCadEditResult Create(const GeometryData::Tube::CTubeNeutralGeometry& Geometry,
            const std::map<std::string,GeometryData::BRepModel>& Bodies = {}) const=0;
        virtual STubeCadEvaluation Evaluate(const GeometryData::Tube::CTubeCadDocument& Document) const=0;
        // Explicit authoring preview. This result never supplies the document's
        // authoritative shape; publishing an edit compiles a new boundary core.
        virtual STubeCadEvaluation EvaluateConstruction(const GeometryData::Tube::CTubeCadDocument& Document) const=0;
        virtual STubeCadEditResult EditGeometry(const GeometryData::Tube::CTubeCadDocument& Original,
            std::uint64_t ExpectedRevision, const GeometryData::Tube::CTubeBoundaryGeometry& Geometry) const=0;
        // A geometrically classified witness for one supplied solid. No scene,
        // resource or authoring metadata enters this neutral calculation.
        virtual GeometryData::Point3 FindInteriorAnchor(const GeometryData::BRepModel& Solid) const=0;
        // Exact planar faces in an explicitly supplied coordinate frame. This
        // reads geometry only; the caller owns editor and document identities.
        virtual GeometryData::Tube::CPlanarRegion2 ReadPlanarRegion(
            const GeometryData::BRepModel& Faces, const GeometryData::Placement3& Frame) const=0;
        virtual std::vector<GeometryData::CurveSegment2> ReadPlanarCurves(
            const GeometryData::BRepModel& Edges, const GeometryData::Placement3& Frame) const=0;
        virtual STubeCadEditResult Edit(const GeometryData::Tube::CTubeCadDocument& Original,
            std::uint64_t ExpectedRevision, const GeometryData::Tube::CTubeNeutralGeometry& Geometry) const=0;
        virtual STubeCadEditResult EditWithBodies(const GeometryData::Tube::CTubeCadDocument& Original,
            std::uint64_t ExpectedRevision, const GeometryData::Tube::CTubeNeutralGeometry& Geometry,
            const std::map<std::string,GeometryData::BRepModel>& AdditionalBodies) const=0;
        virtual STubeCadSketchView ReadSketch(const GeometryData::Tube::CTubeCadDocument& Document,
            const std::string& NodeID) const=0;
        virtual STubeCadEditResult EditSketch(const GeometryData::Tube::CTubeCadDocument& Original,
            const STubeCadSketchView& Sketch) const=0;
        virtual STubeCadEditResult SetSectionDimension(const GeometryData::Tube::CTubeCadDocument& Original,
            std::uint64_t ExpectedRevision, const std::string& NodeID, const std::string& LoopID,
            ETubeSectionDimension Dimension, double Value) const=0;
        virtual std::vector<STubeCadSketchView> ReadSketches(
            const GeometryData::Tube::CTubeCadDocument& Document,
            const std::vector<std::string>& NodeIDs) const=0;
        // Direct core creation, including classified geometry with no authoring
        // interpretation. The placement maps normalized local X to document space.
        virtual STubeCadEditResult CreateGeometry(const GeometryData::Tube::CTubeBoundaryGeometry& Geometry,
            const GeometryData::Placement3& Placement = {}) const=0;
    };
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP STubeCadEvaluation EvaluateTubeCadConstruction(
        const GeometryData::Tube::CTubeCadDocument& Document);
}
