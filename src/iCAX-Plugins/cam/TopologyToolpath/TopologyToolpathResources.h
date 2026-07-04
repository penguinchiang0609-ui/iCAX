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

            bool IsEmpty() const noexcept
            {
                return Faces.empty() && Loops.empty() && Edges.empty();
            }
        };

        /*
        * @brief 根据模型资源 key 生成拓扑资源 key。
        */
        inline std::string MakeCAMTopologyResourceID(IN const std::string& strModelResourceID_)
        {
            return strModelResourceID_.empty() ? std::string() : strModelResourceID_ + "#cam.topology";
        }

        /*
        * @brief 根据模型资源 key 生成显示资源 key。
        */
        inline std::string MakeCAMDisplayResourceID(IN const std::string& strModelResourceID_)
        {
            return strModelResourceID_.empty() ? std::string() : strModelResourceID_ + "#cam.display";
        }
    }
}
