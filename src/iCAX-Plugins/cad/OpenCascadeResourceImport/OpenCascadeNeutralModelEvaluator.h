#pragma once

#include "OpenCascadeResourceImportExport.h"

#include "TemplateRuntime/TemplateContracts.h"

#include <map>
#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>

namespace iCAX::OpenCascade
{
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
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SNeutralModelEvaluation EvaluateNeutralModel(
        const iCAX::TemplateRuntime::SNeutralModel& Model_);

    // Executes only the requested geometry roots and their actual dependencies.
    // Roots are geometry node keys, not item/output-purpose keys. An empty list
    // evaluates nothing; duplicate roots and shared dependencies are evaluated once.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SNeutralModelEvaluation EvaluateNeutralModel(
        const iCAX::TemplateRuntime::SNeutralModel& Model_,
        const std::vector<std::string>& GeometryKeys_);
}
