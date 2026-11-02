#pragma once
#include "OpenCascadeResourceImportExport.h"
#include <TopoDS_Shape.hxx>

namespace iCAX::OpenCascade
{
    // Preserve imported geometry while avoiding OCCT's coincident full-period
    // edge splitter defect. Ordinary operands retain their original topology.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP TopoDS_Shape PrepareOpenCascadeBooleanOperand(const TopoDS_Shape& shape);
}
