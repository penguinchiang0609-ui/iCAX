#pragma once
#include "Data.h"
#include "Variant.h"
#include <sstream>
#include <string>
#include <stdexcept>

namespace iCAX
{
    namespace Data
    {
        class _DATA_EXP VariantSerializer final
        {
        public:
            /*
            * @brief 序列化成字符串
            * @param [in] var
            * @return std::string
            */
            static std::string Serialize(IN const Variant& var);

            /*
            * @brief 反序列化
            * @param [in] json
            * @return Variant
            */
            static Variant Deserialize(IN const std::string& json);

        };
    }
}
