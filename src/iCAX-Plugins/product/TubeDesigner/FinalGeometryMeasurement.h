#pragma once

#include "TubeDesignerExport.h"
#include "Data/Variant.h"

#include <cstdint>
#include <string>

#include <TopoDS_Shape.hxx>

namespace iCAX::TubeDesigner
{
    /*
    * 从最终制造 BRep 观察线性零件和开口尺寸。调用方不得传入模板参数或布尔意图；
    * 返回值只描述 Shape_ 当前实际存在的几何。
    */
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap MeasureFinalPartGeometry(
        IN const TopoDS_Shape& Shape_,
        IN const std::string& strResourceID_,
        IN std::uint64_t nResourceVersion_);
}
