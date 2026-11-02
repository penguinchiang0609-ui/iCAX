#pragma once

#include "Resources/ResourceLibrary.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 根据模型资源 key 生成工件拓扑资源 key。
        */
        inline std::string MakeTopologyResourceID(
            IN const iCAX::Resource::CResourceLibrary& Resources_,
            IN const std::string& strModelResourceID_)
        {
            return strModelResourceID_.empty()
                ? std::string()
                : Resources_.MakeDerivedResourceURL(
                    strModelResourceID_,
                    "topology");
        }

        inline std::string MakeWorkpieceMaterialResourceID(
            IN const iCAX::Resource::CResourceLibrary& Resources_,
            IN const std::string& strModelResourceID_)
        {
            return strModelResourceID_.empty()
                ? std::string()
                : Resources_.MakeDerivedResourceURL(
                    strModelResourceID_,
                    "render.material");
        }
    }
}
