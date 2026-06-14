#pragma once
#include "Data.h"
#include <map>
#include <string>
#include <list>
#include <variant>
#include <typeinfo>
#include <typeindex>
#include "uuid.h"
#include <functional>
#include <optional>
#include "Array1.h"
#include "Array2.h"
#include <vector>

namespace iCAX
{
    namespace Data
    {
        struct Variant;
        typedef std::map<std::string, Variant> ObjectMap;//!< 对象字典
        typedef std::pair<std::string, Variant> ObjectUnit;//!< 对象字典
        typedef std::vector<Variant> VariantArray;   //!< 列表
        using PropertySet = ObjectMap;  //!< 别名，属性集
        using PropertyPair = ObjectUnit;//!< 属性名-属性值 对
        using PropertyArray = VariantArray;//!< 别名，属性列表
        using PropertyValue = Variant; //!< 别名，属性值

        /*
        * @brief 万能变量
        */
        struct _DATA_EXP Variant final
        {
        public:

            //!< 使用 std::variant 支持的类型列表
            using VariantType = std::variant<
                //!< 空值
                std::monostate,
                //!< 基础类型
                //!< 单值
                bool, char, uint8_t, short, unsigned short, int, unsigned int, long long, unsigned long long, float, double, std::string,/* wchar_t, std::wstring,*/ uuid,
                
                //!< 一维数组内置
                Bool1, Bool2, Bool3, Bool4, Bool5, Bool6,
                Byte1, Byte2, Byte3, Byte4, Byte5, Byte6,
                Short1, Short2, Short3, Short4, Short5, Short6,
                Int1, Int2, Int3, Int4, Int5, Int6,
                Long1, Long2, Long3, Long4, Long5, Long6,
                Real1, Real2, Real3, Real4, Real5, Real6,
                Double1, Double2, Double3, Double4, Double5, Double6,
                //!< 二维方阵数组内置
                Bool2x2, Bool3x3, Bool4x4,
                Byte2x2, Byte3x3, Byte4x4,
                Short2x2, Short3x3, Short4x4,
                Int2x2, Int3x3, Int4x4,
                Long2x2, Long3x3, Long4x4,
                Real2x2, Real3x3, Real4x4,
                Double2x2, Double3x3, Double4x4,
                //!< 复杂对象
                ObjectMap,      //!< 对象字典
                VariantArray    //!< 数组
            >;

        public:
            /*
            * @brief 默认构造函数
            */
            Variant();

            /*
            * @brief 构造函数，允许直接初始化任意支持的类型
            */
            template <typename T>
            Variant(IN const T& Value_) : m_Value(Value_) {}

            /*
            * @brief 检查是否存储特定类型
            */
            template <typename T>
            bool Is() const
            {
                return std::holds_alternative<T>(m_Value);
            }

            /*
            * @brief 获取值的模板方法，抛出异常如果类型不匹配
            */
            template <typename T>
            T To() const
            {
                //return std::visit([](auto&& arg) -> T
                //    {
                //        using ArgT = std::decay_t<decltype(arg)>;
                //        if constexpr (std::is_same_v<T, ArgT>)
                //        {
                //            return arg;
                //        }
                //        else if constexpr (std::is_arithmetic_v<ArgT> && std::is_arithmetic_v<T>)
                //        {
                //            // 仅在算术类型之间做转换
                //            return static_cast<T>(arg);
                //        }
                //        else
                //        {
                //            throw std::bad_variant_access();
                //        }
                //    }, m_Value);

                if (!std::holds_alternative<T>(m_Value))
                    throw std::bad_variant_access();

                return std::get<T>(m_Value);

                //return std::visit([](auto&& arg) -> T
                //    {
                //        if constexpr (std::is_same_v<T, std::decay_t<decltype(arg)>>)
                //        {
                //            return arg;
                //        }
                //        else if constexpr (std::is_convertible_v<std::decay_t<decltype(arg)>, T>)//!< 自动类型转换
                //        {
                //            return static_cast<T>(arg);
                //        }
                //        else
                //        {
                //            throw std::bad_variant_access();
                //        }
                //    }, m_Value);
            }

            /*
            * @brief 根据路径寻找叶节点值
            * @param [in] strPath_
            * @return std::optional<Variant>
            */
            std::optional<Variant> GetByPath(IN const std::string& strPath_) const;

            /*
            * @brief 根据路径设置值
            * @param [in] strPath_
            * @param [in] Value_
            */
            void SetByPath(IN const std::string& strPath_, IN const Variant& Value_);

            /*
            * @brief 筛选
            * @param [in] strPath_
            * @param [in] Predicate_
            * @return VariantArray
            */
            VariantArray FilterByPathAndPredicate(IN const std::string& strPath_, IN std::function<bool(const Variant&)> Predicate_) const;

            bool operator==(const Variant& rhs) const;
            bool operator!=(const Variant& rhs) const;
            bool operator<(const Variant& rhs) const;
            bool operator>(const Variant& Right_) const;
            bool operator<=(const Variant& Right_) const;
            bool operator>=(const Variant& Right_) const;

        public:
            /*
            * @brief 空值
            */
            static const Variant Nil;

        public:
            VariantType m_Value;
        };
    }
}
