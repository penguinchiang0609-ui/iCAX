#pragma once

#include <string>

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 根据模型资源 key 生成工件拓扑资源 key。
        */
        inline std::string MakeTopologyResourceID(IN const std::string& strModelResourceID_)
        {
            return strModelResourceID_.empty() ? std::string() : strModelResourceID_ + "#topology";
        }

        inline std::string MakeWorkpieceMaterialResourceID(IN const std::string& strModelResourceID_)
        {
            return strModelResourceID_.empty() ? std::string() : strModelResourceID_ + "#render.material";
        }
    }
}
