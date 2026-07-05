#pragma once

#include "Data/Variant.h"
#include "GeometryData/GeometryData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief CAM 拓扑资源。
        * @details
        *   拓扑资源由模型导入、特征识别或拾取算法生成，属于 Scene.Resources。
        *   这里用 VariantArray 表达是为了保持前端协议和未来算法插件解耦：
        *   - Faces 中的元素应至少包含 id、kind、label、points。
        *   - Loops 中的元素应至少包含 id、kind、label，可选 center、radius、role。
        *   - Edges 中的元素应至少包含 id、kind、label、points。
        */
        struct CCAMTopologyResource final
        {
            uint64_t nVersion = 0;              //!< 拓扑数据版本。
            iCAX::Data::VariantArray Faces;     //!< 可拾取面集合。
            iCAX::Data::VariantArray Loops;     //!< 可拾取 loop 集合。
            iCAX::Data::VariantArray Edges;     //!< 可拾取边集合。
            iCAX::Data::ObjectMap Metadata;      //!< 导入模式、诊断信息和来源版本等元数据。
        };

        /*
        * @brief CAM 刀路曲线资源。
        * @details
        *   Path Entity 通过 CurveResourceID 引用该资源。
        *   当前插件只负责把确认的拓扑目标沉淀为刀路资源壳；真实空间曲线、姿态场和采样结果由后续算法服务填充。
        *   Curve 中的几何数据统一使用项目世界坐标，不保存 TCP、切割头局部坐标或前端渲染坐标。
        */
        struct CCAMPathCurveResource final
        {
            uint64_t nVersion = 0;                    //!< 曲线资源版本。
            std::string TopologyKind;                  //!< 来源拓扑类型：face/loop/edge。
            unsigned long long TopologyID = 0;         //!< 来源拓扑 ID。
            iCAX::Data::ObjectMap SourceTopology;      //!< 来源拓扑对象快照。
            iCAX::GeometryData::CompositeCurve3 Curve; //!< 后续算法填充的世界坐标复合空间曲线。
        };

        /*
        * @brief CAM 姿态场采样点。
        * @details
        *   采样点只描述指定曲线参数处的激光束方向，不保存位置、不反向引用 Path 或 Curve。
        */
        struct SCAMPoseSample final
        {
            uint64_t nSegmentIndex = 0;                     //!< Curve.Segments 中的段索引。
            double dSegmentU = 0.0;                         //!< 对应段的原生曲线参数。
            iCAX::GeometryData::Direction3 BeamDirection;   //!< 世界坐标激光束方向。
        };

        /*
        * @brief CAM 五轴姿态场资源。
        * @details
        *   Path Entity 通过 PoseFieldResourceID 引用该资源。
        *   曲线资源与姿态场资源的对应关系由 Path Entity 上的 CCAMPathComponent 维护。
        *   姿态场以复合曲线的段索引和段原生参数定位采样点：
        *   - segmentIndex：Segments 中的段索引。
        *   - segmentU：该段曲线自己的参数，不做全局归一化，也不使用累计弧长作为主定位。
        *   - beamDirection：世界坐标激光束方向，数组长度为 3。
        *   位置由曲线资源按 segmentIndex + segmentU 求值获得；TCP、完整机械姿态和关节值属于运动规划阶段。
        */
        struct CCAMPoseFieldResource final
        {
            uint64_t nVersion = 0;                   //!< 姿态场资源版本。
            std::vector<SCAMPoseSample> Samples;      //!< 姿态采样点数组。
        };
    }
}
