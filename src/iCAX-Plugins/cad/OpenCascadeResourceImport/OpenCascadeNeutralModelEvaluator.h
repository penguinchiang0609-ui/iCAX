#pragma once

#include "OpenCascadeResourceImportExport.h"

#include "TemplateRuntime/TemplateContracts.h"

#include <cstddef>
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
    };

    struct _OPEN_CASCADE_RESOURCE_IMPORT_EXP SNeutralModelEvaluation final
    {
        std::map<std::string, TopoDS_Shape> Geometry;

        const TopoDS_Shape& At(const std::string& strGeometryKey_) const;
    };

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

    // Dependency-ready nodes run in bounded batches. Mutating OCCT builders
    // receive private geometry; the result cache is committed only by the caller
    // thread. Foundation Task runs work on a reusable dedicated pool; exceptions
    // are rethrown after all batch tasks have completed. Inner OCCT
    // parallelism is disabled to avoid nested thread-pool oversubscription.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SNeutralModelEvaluation EvaluateNeutralModel(
        const iCAX::TemplateRuntime::SNeutralModel& Model_,
        const std::vector<std::string>& GeometryKeys_,
        const SNeutralModelEvaluationOptions& Options_);
}
