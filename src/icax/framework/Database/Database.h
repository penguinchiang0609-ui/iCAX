#pragma once

#ifdef _DATABASE
    #define _DATABASE_EXP __declspec(dllexport)
#else
    #define _DATABASE_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 属性类型
        */
        enum class EPropertyKind
        {
            Value,
            Derived
        };

        /*
        * @brief 属性持久化语义
        */
        enum class EPropertyPersistence
        {
            Persistent,
            NonPersistent
        };

        /*
        * @brief 属性修改传播策略
        */
        enum class EPropertyChangePolicy
        {
            Transactional,
            Observable,
            Silent
        };
    }
}
