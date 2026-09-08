#pragma once

#include "TubeDesignerExport.h"
#include "Data/Variant.h"

#include <cstdint>
#include <string>

#include <TopoDS_Shape.hxx>

namespace iCAX::TubeDesigner
{
    struct SPlanarNestingEnd final
    {
        // Valid supporting KNIFE plane, not a claim that a planar BRep face exists.
        bool IsPlanar = false;
        double Projection = 0.0;
        /** 平面方程在规范化坐标中的轴向梯度：x = GradientY*y + GradientZ*z + c。 */
        double GradientY = 0.0;
        double GradientZ = 0.0;
        double Offset = 0.0;
    };

    struct SLinearNestingGeometry final
    {
        bool IsReliable = false;
        double EnvelopeLength = 0.0;
        double MaterialEquivalentLength = 0.0;
        SPlanarNestingEnd Left;
        SPlanarNestingEnd Right;
        // Bounds of the conservative knife-plane blank in source coordinates.
        // Its centre can differ from the final solid's AABB centre.
        double AxialMinimum = 0.0;
        double AxialMaximum = 0.0;
    };

    /*
    * 将线性制造零件转换到统一加工坐标：长轴沿 +X，实体包围盒中心位于原点。
    * 不会修改输入 Shape_。
    */
    _TUBE_DESIGNER_EXP TopoDS_Shape NormalizeLinearPartForManufacturing(
        IN const TopoDS_Shape& Shape_);

    /**
     * 从沿 +X 的最终 BRep 搜索不削掉成品的左右刀平面；真实端口不必平面。
     * 仅输出直切或保守斜切包络。IsReliable=true 才能使用刀平面投影套切。
     */
    _TUBE_DESIGNER_EXP SLinearNestingGeometry MeasureLinearNestingGeometry(
        IN const TopoDS_Shape& Shape_);

    /*
    * 从最终制造 BRep 观察线性零件和开口尺寸。调用方不得传入模板参数或布尔意图；
    * 返回值只描述 Shape_ 当前实际存在的几何。
    */
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap MeasureFinalPartGeometry(
        IN const TopoDS_Shape& Shape_,
        IN const std::string& strResourceID_,
        IN std::uint64_t nResourceVersion_);

    // Plate dimensions and annotation axes come from the final solid, not a tube section.
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap MeasureFinalPlateGeometry(
        IN const TopoDS_Shape& Shape_,
        IN const std::string& strResourceID_,
        IN std::uint64_t nResourceVersion_);
}
