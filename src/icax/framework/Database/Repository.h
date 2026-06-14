#pragma once
#include <memory>
#include "IRepository.h"
#include "Domain.h"
#include "VersionTable.h"

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 仓储实现
        */
        class CRepository final : public IRepository, public IDomainEventListener, public std::enable_shared_from_this<CRepository>
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] 
            */
            CRepository(IN const iCAX::Data::uuid& UID_);

            /*
            * @brief 析构函数
            */
            virtual ~CRepository();

            //!< 禁用
            CRepository(IN const CRepository&) = delete;
            CRepository(IN const CRepository&&) = delete;
            CRepository& operator= (IN const CRepository&) = delete;
            CRepository& operator= (IN const CRepository&&) = delete;

        public:
            /*
            * @brief 获取ID
            * @return const iCAX::Data::uuid&
            */
            virtual const iCAX::Data::uuid& GetID() const override;

        public:
            /*
            * @brief 初始化
            */
            void Initialzie();

            /*
            * @brief 创建域
            * @param [in] ID_
            * @param [in] bPersistent_ 是否持久化
            * @remark 如果存在多个域，业务层需要持有自己域名的ID
            */
            virtual std::shared_ptr<IDomain> CreateDomain(IN const iCAX::Data::uuid& ID_, IN const bool& bPersistent_) override;

            /*
            * @brief 是否包含域
            * @param [in] ID_
            */
            virtual bool HasDomain(IN const iCAX::Data::uuid& ID_) const override;

            /*
            * @brief 创建域
            * @param [in] ID_
            * @remark 如果存在多个域，业务层需要持有自己域名的ID
            */
            virtual std::shared_ptr<IDomain> GetDomain(IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 移除域
            * @param [in] ID_
            */
            virtual void DeleteDomain(IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 域数量
            * @return int
            */
            virtual int DomainCount() const override;

            /*
            * @brief 获取域ID列表
            * @return std::vector<iCAX::Data::uuid>
            */
            virtual std::vector<iCAX::Data::uuid> GetDomainIDs() const override;

            /*
            * @brief 获取MetaEntity
            * @return std::shared_ptr<IEntity>
            * @remark 仓储自身的描述、配置信息承载者
            */
            virtual std::shared_ptr<IEntity> GetMetaEntity() override;

            /*
            * @brief 清空
            */
            virtual void CleanUp(IN const bool& bForced_ = false) override;

            /*
            * @brief 是否有效
            */
            virtual bool IsValid() override;

            /*
            * @brief 获取组件版本
            * @param [in] nDomainID_
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            * @return size_t
            */
            virtual size_t GetComponentVersion(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const override;

            /*
            * @brief 组件版本升级
            * @param [in] nDomainID_
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            */
            virtual void BumpComponentVersion(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const override;

            /*
            * @brief 是否发生了更改
            * @param [in] nDomainID_
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            * @return bool
            */
            virtual bool IsComponentChanged(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const override;

            /*
            * @brief 重置组件更改标记
            * @details
            *   上层调用，每一帧结束的时候调用一次
            */
            virtual void ResetComponentChangedFlag() override;

        public://!< 域事件观察方法
            /*
            * @brief 域修改前事件
            * @param [in] strPropertyName_ 属性名称
            * @param [in] NewValue_ 属性值
            */
            virtual void OnDomainChanging(IN void* pSender_, IN const DomainEventArgs& Args_) override;

            /*
            * @brief 域更改后事件
            * @param [in] strPropertyName_
            * @param [in] NewValue_ 属性值
            */
            virtual void OnDomainChanged(IN void* pSender_, IN const DomainEventArgs& Args_) override;

        public:
            /*
            * @brief 添加观察者
            * @param [in] Observer_
            */
            virtual void AddObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_) override;

            /*
            * @brief 移除观察者
            * @param [in] Observer_
            */
            virtual void RemoveObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_) override;

        protected:
            /*
            * @brief 前触发
            * @param [in] nType_
            * @param [in] ID_
            * @param [in] strClassName_ 类名
            * @param [in] PreviousValue_ 旧值
            * @param [in] NewValue_ 新值
            */
            void TriggerRepositoryChanging(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<IDomain> pDomain_);

            /*
            * @brief 后触发
            * @param [in] nType_
            * @param [in] ID_
            * @param [in] strClassName_ 类名
            * @param [in] PreviousValue_ 旧值
            * @param [in] NewValue_ 新值
            */
            void TriggerRepositoryChanged(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<IDomain> pDomain_);

        private:
            std::list<std::weak_ptr<IRepositoryEventListener>> m_Observers;

        private:
            iCAX::Data::uuid m_UID;                                                           //!< 仓储ID
            std::unordered_map<iCAX::Data::uuid, std::shared_ptr<CDomain>> m_mapDomains;                //!< 域字典
            std::shared_ptr<VersionTable> m_pVerisonTable;                                     //!< 版本号表
        };
    }
}