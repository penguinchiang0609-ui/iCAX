#pragma once
#include "Database.h"
#include <string>
#include "Data/Variant.h"
#include "Data/uuid.h"
#include <map>
#include <memory>

using namespace iCAX::Data;

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 域事件参数
        */
        struct _DATABASE_EXP DomainEventArgs
        {
            enum EventType
            {
                kModifyComponent = 0,                               //!< 修改组件
                kEnableComponent,                                   //!< 启用组件
                kDisableComponent,                                  //!< 禁用组件
                kAddComponent,                                      //!< 增加组件
                kRemoveComponent,                                   //!< 移除组件
                kAddEntity,                                         //!< 增加实体
                kDeleteEntity,                                      //!< 删除实体
            }nType = kModifyComponent;                                                 //!< 操作类型，即对于组件的操作
            iCAX::Data::uuid DomainID;                            //!< 域ID
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
            std::shared_ptr<class IDomain> pDomain;                 //!< 域
        };

        /*
        * @brief 域事件侦听者
        */
        class _DATABASE_EXP IDomainEventListener
        {
        public:
            /*
            * @brief 构造函数
            */
            IDomainEventListener() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IDomainEventListener() = default;

        public:
            /*
            * @brief 修改前事件
            * @param [in] pSender_
            * @param [in] Args_
            */
            virtual void OnDomainChanging(IN void* pSender_, IN const DomainEventArgs& Args_) = 0;

            /*
            * @brief 更改后事件
            * @param [in] pSender_
            * @param [in] Args_
            */
            virtual void OnDomainChanged(IN void* pSender_, IN const DomainEventArgs& Args_) = 0;
        };

        /*
        * @brief 域事件发布者
        */
        class _DATABASE_EXP IDomianEventPublisher
        {
        public:
            /*
            * @brief 构造函数
            */
            IDomianEventPublisher() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IDomianEventPublisher() = default;

        public:
            /*
            * @brief 添加观察者
            * @param [in] Observer_
            */
            virtual void AddObserver(IN std::shared_ptr<IDomainEventListener> Observer_) = 0;

            /*
            * @brief 移除观察者
            * @param [in] Observer_
            */
            virtual void RemoveObserver(IN std::shared_ptr<IDomainEventListener> Observer_) = 0;
        };

        //typedef DomainEventArgs OperationUnit;
        //typedef IDomainEventListener IOperationListener;
        //typedef IDomianEventPublisher IOperationPublisher;
    }
}
