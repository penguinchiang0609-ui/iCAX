#pragma once

#include "Data/uuid.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 根据独立资源 ID 生成机床定义资源 key。
        * @details
        *   机床定义资源先进入 Scene.Resources，后续 MachineInstanceComponent 再引用它。
        *   这样“导入定义”和“实例化机床”不会被同一个 Machine EntityID 绑死。
        */
        inline std::string MakeMachineDefinitionResourceID(IN const iCAX::Data::uuid& DefinitionID_)
        {
            return DefinitionID_.is_nil() ? std::string() : "machine-definition/" + iCAX::Data::to_string(DefinitionID_) + "#description";
        }

        inline std::string MakeMachineMaterialResourceID(
            IN const std::string& strMachineResourceID_,
            IN const std::string& strAttachmentKind_,
            IN size_t nSourceIndex_)
        {
            return strMachineResourceID_.empty()
                ? std::string()
                : strMachineResourceID_ + "/" + strAttachmentKind_ + "/" + std::to_string(nSourceIndex_) + "#render.material";
        }

        inline std::string MakeMachineVisualMaterialResourceID(IN const std::string& strMachineResourceID_, IN size_t nVisualIndex_)
        {
            return MakeMachineMaterialResourceID(strMachineResourceID_, "visual", nVisualIndex_);
        }

        inline std::string MakeMachineTextureResourceID(
            IN const std::string& strMachineResourceID_,
            IN const std::string& strAttachmentKind_,
            IN size_t nSourceIndex_)
        {
            return strMachineResourceID_.empty()
                ? std::string()
                : strMachineResourceID_ + "/" + strAttachmentKind_ + "/" + std::to_string(nSourceIndex_) + "#render.texture";
        }
    }
}
