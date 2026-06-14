#pragma once
#include "Database.h"
#include <string>
#include "Data/Variant.h"
#include <memory>

using namespace iCAX::Data;
namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 组件事件参数
        */
        struct _DATABASE_EXP ComponentEventArgs final
        {
            enum EventType
            {
                kModifyComponent = 0,
                kEnableComponent,
                kDisableComponent,
            }nType = kModifyComponent;                                                 //!< 操作类型，即对于组件的操作
            std::string strClassName;                               //!< 组件类名
            PropertySet PreviousProperties;                         //!< 旧值
            PropertySet NewProperties;                              //!< 新值
            std::shared_ptr<class CComponentBase> pComponent;       //!< 组件
        };

        /*
        * @brief 组件事件侦听者
        */
        class _DATABASE_EXP IComponentEventListener
        {
        public:
            /*
            * @brief 构造函数
            */
            IComponentEventListener() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IComponentEventListener() = default;

        public:
            /*
            * @brief 组件修改前事件
            * @param [in] strPropertyName_ 属性名称
            * @param [in] NewValue_ 属性值
            */
            virtual void OnComponentChanging(IN void* pSender_, IN const ComponentEventArgs& Args_) = 0;

            /*
            * @brief 组件更改后事件
            * @param [in] strPropertyName_
            * @param [in] NewValue_ 属性值
            */
            virtual void OnComponentChanged(IN void* pSender_, IN const ComponentEventArgs& Args_) = 0;
        };

        /*
        * @brief 组件事件发布者
        */
        class _DATABASE_EXP IComponentEventPublisher
        {
        public:
            /*
            * @brief 构造函数
            */
            IComponentEventPublisher() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IComponentEventPublisher() = default;

        public:
            /*
            * @brief 添加观察者
            * @param [in] Observer_
            */
            virtual void AddObserver(IN std::shared_ptr<IComponentEventListener> Observer_) = 0;

            /*
            * @brief 移除观察者
            * @param [in] Observer_
            */
            virtual void RemoveObserver(IN std::shared_ptr<IComponentEventListener> Observer_) = 0;
        };
    }
}
