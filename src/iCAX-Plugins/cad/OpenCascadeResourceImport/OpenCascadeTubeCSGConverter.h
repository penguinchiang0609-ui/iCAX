#pragma once

#include "GeometryData/TubeNeutralGeometry.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>

#ifdef _OPEN_CASCADE_RESOURCE_IMPORT
#define _OPEN_CASCADE_TUBE_CSG_EXP __declspec(dllexport)
#else
#define _OPEN_CASCADE_TUBE_CSG_EXP __declspec(dllimport)
#endif

namespace iCAX::OpenCascade
{
    struct STubeCSGConversionOptions final
    {
        double Tolerance = 0.001;
        double AngularToleranceRadians = 1.0e-3;
        std::size_t SliceSampleCount = 17;
        std::string SourceBRepResourceID;
        std::string RemovalBRepResourceID;
        std::string AdditionBRepResourceID;
    };

    struct STubeCSGConversionResult final
    {
        iCAX::GeometryData::Tube::CTubeNeutralGeometry Geometry;
        TopoDS_Shape BaseShape;
        TopoDS_Shape RemovalShape;
        TopoDS_Shape AdditionShape;
        // 节点对应的完整构造体，用于历史特征选择与半透明预览。
        // 它与 ShapeInsideBase 不同：贯切特征会保留伸出毛坯之外的生成体。
        std::map<std::string, TopoDS_Shape> PreviewShapes;
    };

    struct STubeCSGEvaluationOptions final
    {
        double Tolerance = 0.001;
        /* 实时编辑可只求值目标及其依赖，正式重建默认生成全部构造体。 */
        bool EvaluateAllConstructionBodies = true;
        /* Composite/Residual 节点引用的资源由调用方从当前 Scene 资源池解析。 */
        std::map<std::string, TopoDS_Shape> ExternalBRepShapes;
    };

    struct STubeCSGEvaluationResult final
    {
        bool bOK = false;
        TopoDS_Shape Shape;
        /* 每个已成功求值节点的完整构造体，用于历史树灵体。 */
        std::map<std::string, TopoDS_Shape> NodeShapes;
        /* 主体外轮廓和各腔体各自的构造体。 */
        std::map<std::string, TopoDS_Shape> SectionPrimitiveShapes;
        std::vector<std::string> Diagnostics;
    };

    /*
    * @brief 从 OCC BRep 恢复规范化 Tube CSG。
    * @details 不依赖直端面或 OCC 解析面类型；用 B 区分继承/生成边界，以生成侧壁切平面的
    *          共同射影中心识别拉伸方向或有限锥心，再用虚拟截面恢复材料 Region。
    *          特征候选保留最终零件 P 的碎面证据，B-P 负责体积约束和覆盖验证。
    *          相交特征通过“候选是缺失体子集 + 候选并集等价”的方式拆解；只把无法继续
    *          参数化的局部降级为带解析曲面证据的 CompositeVolume，最后才使用 ResidualBRep。
    */
    _OPEN_CASCADE_TUBE_CSG_EXP STubeCSGConversionResult ConvertBRepToTubeCSG(
        IN const TopoDS_Shape& Shape_,
        IN const STubeCSGConversionOptions& Options_ = {});

    /*
    * @brief 对 Tube 中性 CSG 重新求值，生成最终 BRep 和各参数化构造体。
    * @details 求值失败不会返回旧 BRep 兜底；调用方应保持原模型不变并向用户报告诊断。
    */
    _OPEN_CASCADE_TUBE_CSG_EXP STubeCSGEvaluationResult EvaluateTubeCSG(
        IN const iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_,
        IN const STubeCSGEvaluationOptions& Options_ = {});
}
