#pragma once
#include "IEntity.h"
#include "Data/uuid.h"
#include <map>
#include <string>
#include "ComponentBase.h"
#include "IEntityEvent.h"

namespace iCAX
{
    namespace Database
    {
        class IRepository;

        /*
        * @brief 实体
        * @remark 
        *   1、实体只有ID与类型信息，不直接拥有其他信息
        *   2、实体的立体描述有一系列承载各个切面信息的组件构成
        *   3、继承组件事件发布者，负责对外发布组件发生的修改
        *   4、继承属性事件侦听者，用于接受组件中属性发生的更改，转成组件的事件对外发布
        */
        class CEntity final : public IComponentEventListener, public IEntity, public std::enable_shared_from_this<CEntity>
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] UID_
            */
            CEntity(IN std::shared_ptr<IRepository> pRepository_, IN const iCAX::Data::uuid& UID_);

            /*
            * @brief 析构函数
            */
            virtual ~CEntity();

            //!< IEntityEventPublisher 成员
        public:
            /*
            * @brief 添加观察者
            * @param [in] Observer_
            */
            virtual void AddObserver(IN std::shared_ptr<IEntityEventListener> Observer_) override;

            /*
            * @brief 移除观察者
            * @param [in] Observer_
            */
            virtual void RemoveObserver(IN std::shared_ptr<IEntityEventListener> Observer_) override;

        protected:
            /*
            * @brief 前触发
            * @param [in] nType_
            * @param [in] strClassName_ 类名
            * @param [in] PreviousValue_ 旧值
            * @param [in] NewValue_ 新值
            * @param [in] pComponent_ 关联组件
            */
            void TriggerEntityChanging(IN const EntityEventArgs::EventType& nType_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_);

            /*
            * @brief 后触发
            * @param [in] nType_
            * @param [in] strClassName_ 类名
            * @param [in] PreviousValue_ 旧值
            * @param [in] NewValue_ 新值
            * @param [in] pComponent_关联组件
            */
            void TriggerEntityChanged(IN const EntityEventArgs::EventType& nType_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_);

        private:
            std::list<std::weak_ptr<IEntityEventListener>> m_Observers;

            //!< IComponentEventListener 成员
        public:
            /*
            * @brief 属性修改前事件
            * @param [in] strPropertyName_ 属性名称
            * @param [in] NewValue_ 属性值
            */
            virtual void OnComponentChanging(IN void* pSender_, IN const ComponentEventArgs& Args_) override;

            /*
            * @brief 属性更改后事件
            * @param [in] strPropertyName_
            * @param [in] NewValue_ 属性值
            */
            virtual void OnComponentChanged(IN void* pSender_, IN const ComponentEventArgs& Args_) override;

            //!< IEntity 成员
        public:
            /*
            * @brief 获取ID
            * @return const iCAX::Data::uuid&
            */
            virtual const iCAX::Data::uuid& GetID() const override;

            /*
            * @brief 添加或替换组件
            * @param [in] strClassName_
            * @return std::shared_ptr<CComponentBase>
            */
            virtual std::shared_ptr<CComponentBase> AddComponent(IN const std::string& strClassName_) override;

            /*
            * @brief 移除组件
            * @param [in] strClassName_
            */
            virtual void RemoveComponent(IN const std::string& strClassName_) override;

            /*
            * @brief 获取组件
            * @param [in] strClassName_
            * @return  std::shared_ptr<CComponentBase>
            */
            virtual std::shared_ptr<CComponentBase> GetComponent(IN const std::string& strClassName_) const override;

            /*
            * @brief 获取组件集
            * @param [in] strClassName_ 父类
            * @return  std::shared_ptr<CComponentBase>
            */
            virtual std::vector<std::shared_ptr<CComponentBase>> GetComponents(IN const std::string& strClassName_) const override;

            /*
            * @brief 是否含有指定组件
            * @param [in] strClassName_
            * @return bool
            */
            virtual bool HasComponent(IN const std::string& strClassName_) const override;

            /*
            * @brief 获取组件类型列表
            * @return std::vector<std::string>
            */
            virtual std::vector<std::string> GetComponentClasses() const override;

            /*
            * @brief 获取组件数量
            * @return int
            */
            virtual int ComponentsCount() const override;

            /*
            * @brief 清除所有组件
            */
            virtual void Cleanup() override;

            /*
            * @brief 获取所在仓储
            * @return std::shared_ptr<class IRepository>
            */
            virtual class std::shared_ptr<IRepository> GetRepository() const override;

        private:
            iCAX::Data::uuid m_UID;                                                               //!< 实体ID
            std::map<std::string, std::shared_ptr<CComponentBase>> m_mapComponents;                 //!< 组件列表
            std::vector<std::string> m_ComponentClasses;
            std::weak_ptr<IRepository> m_pRepository;                                                //!< 所在仓储
        };
    }
}
