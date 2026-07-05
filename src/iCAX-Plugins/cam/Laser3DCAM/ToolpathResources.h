#pragma once

#include "Data/Variant.h"

#include <cstdint>
#include <string>

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief CAM 导入模型资源。
        * @details
        *   该对象是 Project.Resources 中的运行期模型资源描述。
        *   真实 STEP/OCC/BRep 对象后续由模型导入插件填充，本插件只定义产品侧需要识别的资源身份。
        */
        struct CCAMImportedModelResource final
        {
            std::string SourcePath;      //!< 原始模型来源路径或 URI。
            std::string DisplayName;     //!< UI 展示名称。
            uint64_t nVersion = 0;       //!< 资源内容版本，可用于与 PDO dataVersion 对齐。
        };

        /*
        * @brief CAM 拓扑资源。
        * @details
        *   拓扑资源由模型导入、特征识别或拾取算法生成，属于项目资源池。
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
        };

        /*
        * @brief CAM 刀路曲线资源。
        * @details
        *   Path Entity 通过 CurveResourceID 引用该资源。
        *   当前插件只负责把确认的拓扑目标沉淀为刀路资源壳；真实空间曲线、姿态场和采样结果由后续算法服务填充。
        *   Segments 中的几何数据统一使用项目世界坐标，不保存 TCP、切割头局部坐标或前端渲染坐标。
        */
        struct CCAMPathCurveResource final
        {
            uint64_t nVersion = 0;                    //!< 曲线资源版本。
            std::string TopologyKind;                  //!< 来源拓扑类型：face/loop/edge。
            unsigned long long TopologyID = 0;         //!< 来源拓扑 ID。
            iCAX::Data::ObjectMap SourceTopology;      //!< 来源拓扑对象快照。
            iCAX::Data::VariantArray Segments;         //!< 后续算法填充的空间曲线段。
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
            uint64_t nVersion = 0;                  //!< 姿态场资源版本。
            iCAX::Data::VariantArray Samples;        //!< 姿态采样点数组，元素为 ObjectMap。
        };
    }
}
