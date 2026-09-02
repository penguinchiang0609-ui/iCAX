#pragma once

#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"

#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>

namespace iCAX::OpenCascade
{
    struct SOpenCascadeBRepBuildResult final
    {
        bool bOK = false;
        TopoDS_Shape Shape;
        std::vector<std::string> Diagnostics;
    };

    /*
    * @brief 从 iCAX 中性 BRep 重建 OCC Shape。
    * @details CAD 编辑必须以资源池中的规范化 BRep 为事实源，不能依赖仍然存在的原 STEP 文件。
    */
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SOpenCascadeBRepBuildResult BuildOpenCascadeShape(
        IN const iCAX::GeometryData::BRepModel& Geometry_);
}
