#pragma once

#include "Data/uuid.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 根据机床实例 EntityID 生成实例资源命名空间。
        * @details 机床定义源文件不会作为资源入库；实例化后的 mesh、material、texture 等资源
        *   仍需要稳定 key，因此直接使用 Machine EntityID 派生命名空间。
        */
        inline std::string MakeMachineInstanceResourceScopeID(IN const iCAX::Data::uuid& MachineID_)
        {
            return MachineID_.is_nil() ? std::string() : "machine/" + iCAX::Data::to_string(MachineID_);
        }

        inline std::string MakeMachineMaterialResourceID(
            IN const std::string& strResourceScopeID_,
            IN const std::string& strAttachmentKind_,
            IN size_t nSourceIndex_)
        {
            return strResourceScopeID_.empty()
                ? std::string()
                : strResourceScopeID_ + "/" + strAttachmentKind_ + "/" + std::to_string(nSourceIndex_) + "#render.material";
        }

        inline std::string MakeMachineVisualMaterialResourceID(IN const std::string& strResourceScopeID_, IN size_t nVisualIndex_)
        {
            return MakeMachineMaterialResourceID(strResourceScopeID_, "visual", nVisualIndex_);
        }

        inline std::string MakeMachineTextureResourceID(
            IN const std::string& strResourceScopeID_,
            IN const std::string& strAttachmentKind_,
            IN size_t nSourceIndex_)
        {
            return strResourceScopeID_.empty()
                ? std::string()
                : strResourceScopeID_ + "/" + strAttachmentKind_ + "/" + std::to_string(nSourceIndex_) + "#render.texture";
        }
    }
}
