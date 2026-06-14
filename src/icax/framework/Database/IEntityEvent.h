#pragma once
#include "Database.h"
#include <string>
#include "Data/uuid.h"
#include <map>
#include "Data/Variant.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 实体事件
        */
        struct _DATABASE_EXP EntityEventArgs
        {
            enum EventType
            {
                kModifyComponent = 0,
                kEnableComponent,
                kDisableComponent,
                kAddComponent,
                kRemoveComponent,
            }nType = kModifyComponent;                                                 //!< 操作类型，即对于组件的操作
            iCAX::Data::uuid EntityID;                            //!< 实体ID
            std::string strClassName;                               //!< 组件类型
            //!< add时，旧值为空，新值为组件的值
            //!< remove时，旧值为组件的值，新值为空
            //!< modify时，旧值为对应字段原来的值，新值为对应字段后来的值
            //!< replace时，旧值为组件原来的值，新值为组件后来的值
            PropertySet PreviousProperties;                         //!< 旧值
            PropertySet NewProperties;                              //!< 新值
            std::shared_ptr<class CComponentBase> pComponent;       //!< 组件
            std::shared_ptr<class IEntity> pEntity;                 //!< 实体
        };

        /*
        * @brief 实体事件侦听者
        */
        class _DATABASE_EXP IEntityEventListener
        {
        public:
            /*
            * @brief 构造函数
            */
            IEntityEventListener() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IEntityEventListener() = default;

        public:
            /*
            * @brief 修改前事件
            * @param [in] strPropertyName_ 属性名称
            * @param [in] NewValue_ 属性值
            */
            virtual void OnEntityChanging(IN void* pSender_, IN const EntityEventArgs& Args_) = 0;

            /*
            * @brief 更改后事件
            * @param [in] strPropertyName_
            * @param [in] NewValue_ 属性值
            */
            virtual void OnEntityChanged(IN void* pSender_, IN const EntityEventArgs& Args_) = 0;
        };

        /*
        * @brief 实体事件发布者
        */
        class _DATABASE_EXP IEntityEventPublisher
        {
        public:
            /*
            * @brief 构造函数
            */
            IEntityEventPublisher() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IEntityEventPublisher() = default;

        public:
            /*
            * @brief 添加观察者
            * @param [in] Observer_
            */
            virtual void AddObserver(IN std::shared_ptr<IEntityEventListener> Observer_) = 0;

            /*
            * @brief 移除观察者
            * @param [in] Observer_
            */
            virtual void RemoveObserver(IN std::shared_ptr<IEntityEventListener> Observer_) = 0;
        };
    }
}
