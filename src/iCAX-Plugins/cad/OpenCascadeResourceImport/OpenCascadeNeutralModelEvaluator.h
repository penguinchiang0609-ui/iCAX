#pragma once

#include "OpenCascadeResourceImportExport.h"

#include "TemplateRuntime/TemplateContracts.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>

namespace iCAX::OpenCascade
{
    struct SNeutralModelEvaluationOptions final
    {
        // 0 selects up to eight workers, leaving one logical CPU for the UI.
        // 1 provides a serial fallback and reproducible performance baseline.
        std::size_t MaximumConcurrency = 0;
        bool UseBoundingBoxFilter = true;
        // The caller observes only the selected roots, allowing unobserved
        // single-consumer plain subtraction chains to execute as one exact cut.
        // Intermediate keepConnectedTo selections are never combined.
        bool CombineUnobservedSubtractions = false;
        // Execute repeated congruent profiles/cutters once in their exact
        // local frame, publishing located results under the authored keys.
        bool ReuseRigidPrototypes = false;
        // Strict execution-only identities. Only unobserved, single-consumer
        // intermediates with fully understood arguments may be bypassed.
        // Intersect extrusions only when their complete profiles share the
        // same frame and extrusion parameters; retain the exact planar BRep.
        bool IntersectMatchingExtrusions = false;
        // A - (B union C) can classify B and C in the same multi-tool cut.
        // Selected/shared union and transform intermediates remain boundaries.
        bool ExpandSubtractionToolUnions = false;
        // This evaluator returns shapes only. Callers that need no OCCT
        // Generated/Modified history can omit that separate bookkeeping.
        bool SuppressUnusedBooleanHistory = false;
        // OCCT's conservative oriented bounds may reduce intersection pairs;
        // this affects candidate screening only, never precision or validation.
        bool UseOrientedBoundingBoxes = false;
        // Called synchronously on the coordinating caller after each unique
        // selected authored key is published. No additional roots are evaluated.
        // The shape remains owned by the result cache; mesh only a private copy.
        // A callback exception joins in-flight geometry tasks before propagating.
        std::function<void(const std::string&, const TopoDS_Shape&)> OnSelectedRootReady;
    };

    struct _OPEN_CASCADE_RESOURCE_IMPORT_EXP SNeutralModelEvaluation final
    {
        std::map<std::string, TopoDS_Shape> Geometry;

        const TopoDS_Shape& At(const std::string& strGeometryKey_) const;
    };

    // Shared exact-shape contract for resource nodes and read-only CAD previews.
    // Accepts only valid, bounded, positive-volume solids or non-empty trees of
    // solids. Does not serialize, copy, transform, heal or triangulate the shape.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP void ValidateSolidResourceShape(
        const TopoDS_Shape& Shape_, const std::string& Context_);

    // Read-only display admission: bounded, closed, positive-volume solids.
    // Full edge/curve consistency checks belong to the actual resource import.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP void ValidateSolidPreviewShape(
        const TopoDS_Shape& Shape_, const std::string& Context_);

    // Executes the generic neutral-model geometry graph with OpenCascade.
    // This adapter intentionally has no knowledge of product domains or templates.
    // Transform nodes require one input and arguments.placement containing origin,
    // xAxis, yAxis and zAxis: finite three-number vectors with unit, orthogonal,
    // right-handed axes. They create located instances sharing the input TShape;
    // Boolean nodes use non-destructive input handling to keep instances independent.
    // Resource nodes have no inputs and require resolved arguments.brep containing
    // at most kMaximumResourceBRepBytes bytes of ASCII BRepTools text. Only valid,
    // bounded, positive-volume solids (or compounds of solids) are accepted. This
    // layer never opens arguments.reference, paths or URLs; hosts resolve them first.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SNeutralModelEvaluation EvaluateNeutralModel(
        const iCAX::TemplateRuntime::SNeutralModel& Model_);

    // Executes only the requested geometry roots and their actual dependencies.
    // Roots are geometry node keys, not item/output-purpose keys. An empty list
    // evaluates nothing; duplicate roots and shared dependencies are evaluated once.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SNeutralModelEvaluation EvaluateNeutralModel(
        const iCAX::TemplateRuntime::SNeutralModel& Model_,
        const std::vector<std::string>& GeometryKeys_);

    // Dependency-ready nodes run with bounded concurrency; completed nodes
    // immediately release their dependants without a batch/level barrier. OCCT builders
    // receive private geometry; the result cache is committed only by the caller
    // thread. Foundation Task runs work on a reusable dedicated pool; exceptions
    // stop scheduling new work and are rethrown after in-flight tasks are joined. Inner OCCT
    // parallelism is disabled to avoid nested thread-pool oversubscription.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SNeutralModelEvaluation EvaluateNeutralModel(
        const iCAX::TemplateRuntime::SNeutralModel& Model_,
        const std::vector<std::string>& GeometryKeys_,
        const SNeutralModelEvaluationOptions& Options_);
}
