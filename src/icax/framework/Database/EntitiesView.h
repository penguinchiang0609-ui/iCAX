#pragma once
#include "Database.h"
#include "IDomainEvent.h"
#include "ComponentMask.h"
#include <unordered_set>
#include "IEntitiesView.h"
#include "IDomain.h"
#include <memory>
#include <set>


namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 实体视图
        */
        class _DATABASE_EXP CEntitiesView final : public IDomainEventListener, public IEntitiesView
        {
        public:
            /*
            * @brief 构造函数
            */
            CEntitiesView(IN std::shared_ptr<IDomain> pDomain_);

            /*
            * @brief 析构函数
            */
            virtual ~CEntitiesView();

        public:
            /*
            * @brief 修改前事件
            * @param [in] pSender_
            * @param [in] Args_
            */
            virtual void OnDomainChanging(IN void* pSender_, IN const DomainEventArgs& Args_) override;

            /*
            * @brief 更改后事件
            * @param [in] pSender_
            * @param [in] Args_
            */
            virtual void OnDomainChanged(IN void* pSender_, IN const DomainEventArgs& Args_) override;

        public:
            /*
            * @brief 构建缓存
            * @param [in] strClassName_ 组件类名
            * @param [in] bForceReset_ 是否强制重建
            */
            virtual void BuildCache(IN const std::string& strClassName_, IN const bool bForceReset_ = false) override;

            /*
            * @brief 获取缓存
            * @param [in] strClassName_ 组件类名
            * @return std::vector<std::shared_ptr<CComponentBase>>
            */
            virtual std::vector<std::shared_ptr<CComponentBase>> GetEntities(IN const std::string& strClassName_) override;

            /*
            * @brief 获取前缓存
            * @param [in] strClassName_ 组件类名
            * @return std::vector<std::shared_ptr<CComponentBase>>
            */
            virtual std::vector<std::shared_ptr<CComponentBase>> GetPreEntities(IN const std::string& strClassName_) override;

            /*
            * @brief 刷线前缓存
            */
            virtual void RefreshPreCache() override;

        private:
            std::unordered_map<iCAX::Data::uuid, CComponentMask> m_EntityMask;
            std::unordered_map<size_t, std::set<std::weak_ptr<CComponentBase>, std::owner_less<std::weak_ptr<CComponentBase>>>> m_Cache;  //!< 缓存数据
            std::unordered_map<size_t, std::set<std::weak_ptr<CComponentBase>, std::owner_less<std::weak_ptr<CComponentBase>>>> m_PreCache;  //!< 缓存数据
            std::weak_ptr<IDomain> m_pDomain;
        };
    }
}
