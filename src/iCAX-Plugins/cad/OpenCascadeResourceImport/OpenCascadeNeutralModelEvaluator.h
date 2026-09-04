#pragma once

#include "OpenCascadeResourceImportExport.h"

#include "TemplateRuntime/TemplateContracts.h"

#include <map>
#include <string>

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
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SNeutralModelEvaluation EvaluateNeutralModel(
        const iCAX::TemplateRuntime::SNeutralModel& Model_);
}
