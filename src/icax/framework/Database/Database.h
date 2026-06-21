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
        * @details
        *   Value 是真实存储字段；Derived 是由 Evaluator 计算出来的派生字段。
        *   派生字段不应直接进入撤销/日志的事实操作序列。
        */
        enum class EPropertyKind
        {
            Value,   //!< 普通值字段。
            Derived  //!< 派生字段，通过依赖字段计算。
        };

        /*
        * @brief 属性持久化语义
        * @details
        *   该语义由字段 meta 声明，保存/快速保存日志可据此过滤字段。
        */
        enum class EPropertyPersistence
        {
            Persistent,    //!< 持久化字段，可进入项目文件或快速保存日志。
            NonPersistent  //!< 非持久化字段，只在运行期存在。
        };

        /*
        * @brief 属性修改传播策略
        * @details
        *   Transactional 参与撤销还原、事务和日志。
        *   Observable 触发事件和版本，但不进入撤销还原/事务日志。
        *   Silent 是运行时裸字段，不触发版本和事件。
        */
        enum class EPropertyChangePolicy
        {
            Transactional, //!< 参与事件、版本、撤销还原、事务和日志。
            Observable,    //!< 触发事件和版本，但不进入撤销还原/事务日志。
            Silent         //!< 运行时裸字段，不触发事件和版本。
        };
    }
}
