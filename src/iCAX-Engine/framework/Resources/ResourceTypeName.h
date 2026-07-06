#pragma once

#include "ResourcesExport.h"

#include <concepts>
#include <string>
#include <type_traits>
#include <typeinfo>

namespace iCAX
{
    namespace Resource
    {
        /*
        * @brief 获取资源类型的稳定名称。
        * @details
        *   资源数据结构可以声明 `inline static constexpr const char* kResourceTypeName`。
        *   这样产品 manifest 可以按 `geometry.brep`、`image.rgba` 等稳定 ID 选择导入导出实现。
        *   未声明稳定名称的类型回退到 `typeid(T).name()`，仅适合本进程内临时使用。
        */
        template <typename T>
        std::string GetResourceTypeName()
        {
            using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
            if constexpr (requires { { TResource::kResourceTypeName } -> std::convertible_to<const char*>; })
            {
                return TResource::kResourceTypeName;
            }
            else
            {
                return typeid(TResource).name();
            }
        }
    }
}
