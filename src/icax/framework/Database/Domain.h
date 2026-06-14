#pragma once
#include "Data/uuid.h"
#include <map>
#include <vector>
#include "IDomain.h"
#include "IEntityEvent.h"
#include "Entity.h"
#include "IRepository.h"
#include "EntitiesView.h"

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 域
        * @remark 
        *   1、每个域为独自存在，彼此之间不透明
        *   2、域的ID由上层业务逻辑层维护，数据层只是维护数据
        */
        class CDomain final : public IDomain, public IEntityEventListener, public std::enable_shared_from_this<CDomain>
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] pRepository_
            * @param [in] UID_
            * @param [in] bPersistent_ 是否持久化
            */
            CDomain(IN std::shared_ptr<IRepository> pRepository_, IN const iCAX::Data::uuid& UID_, IN const bool& bPersistent_);

            /*
            * @brief 析构函数
            */
            virtual ~CDomain();

        public:
            /*
            * @brief 获取ID
            * @return iCAX::Data::uuid
            */
            virtual iCAX::Data::uuid GetID() const override;

        public:
            /*
            * @brief 初始化
            */
            void Initialize();

            /*
            * @brief 创建实体
            * @param [in] ID_
            */
            virtual std::shared_ptr<IEntity> CreateEntity(IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 是否包含实体 
            * @param [in] ID_
            */
            virtual bool HasEntity(IN const iCAX::Data::uuid& ID_) const override;

            /*
            * @brief 创建实体
            * @param [in] ID_
            */
            virtual std::shared_ptr<IEntity> GetEntity(IN const iCAX::Data::uuid& ID_) const override;

            /*
            * @brief 移除实体
            * @param [in] ID_
            */
            virtual void DeleteEntity(IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 筛选实体
            * @param [in] funcWhere_
            * @return std::vector<std::shared_ptr<CEntity>>
            */
            virtual std::vector<std::shared_ptr<IEntity>> FilterEntities(IN std::function<bool(std::shared_ptr<IEntity>)> funcWhere_) const override;

            /*
            * @brief 实体数量
            * @return int
            */
            virtual int EntityCount() const override;

            /*
            * @brief 获取实体的ID列表
            * @return std::vector<iCAX::Data::uuid>
            */
            virtual std::vector<iCAX::Data::uuid> GetEntityIDs() const override;

            /*
            * @brief 清空
            * @param [in] bForced_ 是否强制，默认false。如果为true，则本体实体也会被删除
            */
            virtual void CleanUp(IN const bool& bForced_ = false) override;

            /*
            * @brief 是否有效
            */
            virtual bool IsValid() override;

        public://!< 实体事件观察方法
            /*
            * @brief 组件修改前事件
            * @param [in] strPropertyName_ 属性名称
            * @param [in] NewValue_ 属性值
            */
            virtual void OnEntityChanging(IN void* pSender_, IN const EntityEventArgs& Args_) override;

            /*
            * @brief 组件更改后事件
            * @param [in] strPropertyName_
            * @param [in] NewValue_ 属性值
            */
            virtual void OnEntityChanged(IN void* pSender_, IN const EntityEventArgs& Args_) override;

        public:
            /*
            * @brief 添加观察者
            * @param [in] Observer_
            */
            virtual void AddObserver(IN std::shared_ptr<IDomainEventListener> Observer_) override;

            /*
            * @brief 移除观察者
            * @param [in] Observer_
            */
            virtual void RemoveObserver(IN std::shared_ptr<IDomainEventListener> Observer_) override;

        protected:
            /*
            * @brief 前触发
            * @param [in] nType_
            * @param [in] ID_
            * @param [in] strClassName_ 类名
            * @param [in] PreviousValue_ 旧值
            * @param [in] NewValue_ 新值
            */
            void TriggerDomainChanging(IN const DomainEventArgs::EventType& nType_, IN const iCAX::Data::uuid& ID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_);

            /*
            * @brief 后触发
            * @param [in] nType_
            * @param [in] ID_
            * @param [in] strClassName_ 类名
            * @param [in] PreviousValue_ 旧值
            * @param [in] NewValue_ 新值
            */
            void TriggerDomainChanged(IN const DomainEventArgs::EventType& nType_, IN const iCAX::Data::uuid& ID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_);

        private:
            std::list<std::weak_ptr<IDomainEventListener>> m_Observers;

        public:
            /*
            * @brief 获取所在仓储
            * @return std::shared_ptr<IRepository>
            */
            virtual std::shared_ptr<IRepository> GetRepository() const override;

            /*
            * @brief 获取MetaEntity
            * @return std::shared_ptr<IEntity>
            * @remark 域需要一些属性字段来描述域的作用，如该域是仿真、刀路生成等等。
            */
            virtual std::shared_ptr<IEntity> GetMetaEntity() const override;

            /*
            * @brief 获取实体视图
            * @return IEntitiesView&
            */
            virtual IEntitiesView& GetView() const override;

            /*
            * @brief 是否是持久化的
            * @details
            *   是否需要持久化。如零件编辑域为临时域，该方法应该返回false，在文件保存的时候无需持久化
            */
            virtual bool IsPersistent()override;

        private:
            iCAX::Data::uuid m_UID;                                                             //!< 域ID
            std::map<iCAX::Data::uuid, std::shared_ptr<CEntity>> m_mapEntities;                 //!< 实体列表
            std::weak_ptr<IRepository> m_pRepository;                                           //!< 所属仓储
            std::shared_ptr<CEntitiesView> m_pEntitesView;                                      //!< 视图数据
            bool m_bPersistent;
        };
    }
}