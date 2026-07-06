#pragma once

#include "Data/uuid.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 根据模型资源 key 生成拓扑资源 key。
        */
        inline std::string MakeCAMTopologyResourceID(IN const std::string& strModelResourceID_)
        {
            return strModelResourceID_.empty() ? std::string() : strModelResourceID_ + "#cam.topology";
        }

        /*
        * @brief 根据模型资源 key 生成显示对象 key。
        * @details 该 key 只用于数据库记录和 RenderService 映射，不要求资源池内一定存在同名资源。
        */
        inline std::string MakeCAMDisplayResourceID(IN const std::string& strModelResourceID_)
        {
            return strModelResourceID_.empty() ? std::string() : strModelResourceID_ + "#render.mesh";
        }

        /*
        * @brief 根据刀路 EntityID 生成刀路曲线资源 key。
        */
        inline std::string MakeCAMPathCurveResourceID(IN const iCAX::Data::uuid& PathEntityID_)
        {
            return PathEntityID_.is_nil() ? std::string() : "cam.path/" + iCAX::Data::to_string(PathEntityID_) + "#curve";
        }

        /*
        * @brief 根据刀路 EntityID 生成姿态场资源 key。
        */
        inline std::string MakeCAMPoseFieldResourceID(IN const iCAX::Data::uuid& PathEntityID_)
        {
            return PathEntityID_.is_nil() ? std::string() : "cam.path/" + iCAX::Data::to_string(PathEntityID_) + "#pose-field";
        }
    }
}
