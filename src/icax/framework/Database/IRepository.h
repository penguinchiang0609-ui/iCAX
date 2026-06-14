#pragma once
#include "Database.h"
#include <memory>
#include "IDomain.h"
#include "IRepositoryEvent.h"

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 仓储
        */
        class _DATABASE_EXP IRepository : public IRepositoryEventPublisher
        {
        public:
            IRepository() = default;
            virtual ~IRepository() = default;
            //!< 禁用
            IRepository(IN const IRepository&) = delete;
            IRepository(IN const IRepository&&) = delete;
            IRepository& operator=(IN const IRepository&) = delete;
            IRepository& operator=(IN const IRepository&&) = delete;

        public:
            /*
            * @brief 获取ID
            * @return const iCAX::Data::uuid&
            * @remark 此处给予仓储唯一ID，是为了以后可能存在多仓储留下可扩展能力
            *   以后希望的是，同一个app可以同时支持打开平面、平坡、管材、三维五轴等项目，或者单纯的平面多开
            */
            virtual const iCAX::Data::uuid& GetID() const = 0;

        public:
            /*
            * @brief 创建域
            * @param [in] ID_
            * @param [in] bPersistent_ 是否持久化
            * @remark 如果存在多个域，业务层需要持有自己域名的ID
            */
            virtual std::shared_ptr<IDomain> CreateDomain(IN const iCAX::Data::uuid& ID_, IN const bool& bPersistent_) = 0;

            /*
            * @brief 是否包含域
            * @param [in] ID_
            */
            virtual bool HasDomain(IN const iCAX::Data::uuid& ID_) const = 0;

            /*
            * @brief 获取域
            * @param [in] ID_
            * @remark 如果存在多个域，业务层需要持有自己域名的ID
            */
            virtual std::shared_ptr<IDomain> GetDomain(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 移除域
            * @param [in] ID_
            */
            virtual void DeleteDomain(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 域数量
            * @return int
            */
            virtual int DomainCount() const = 0;

            /*
            * @brief 获取域ID列表
            * @return std::vector<iCAX::Data::uuid>
            */
            virtual std::vector<iCAX::Data::uuid> GetDomainIDs() const = 0;

            /*
            * @brief 获取MetaEntity
            * @return std::shared_ptr<IEntity>
            * @remark 仓储自身的描述、配置信息承载者
            */
            virtual std::shared_ptr<IEntity> GetMetaEntity() = 0;

            /*
            * @brief 清空
            * @param [in] bForced_ 是否强制，默认false。如果为true，则本体域也会被删除
            */
            virtual void CleanUp(IN const bool& bForced_ = false) = 0;

            /*
            * @brief 是否有效
            * @details
            *   如果缺失了本体域，则为无效
            */
            virtual bool IsValid() = 0;

            /*
            * @brief 获取组件版本
            * @param [in] Component_
            * @return size_t
            */
            size_t GetComponentVersion(IN const CComponentBase& Component_) const
            {
                auto _pEntity = Component_.GetEntity();
                if (!_pEntity)
                {
                    return -1;
                }
                auto _pDomain = _pEntity->GetDomain();
                if (!_pDomain)
                {
                    return -1;
                }
                return GetComponentVersion(_pDomain->GetID(), _pEntity->GetID(), Component_.GetComponentClass());
            }

            /*
            * @brief 是否发生了更改
            * @param [in] Component_
            * @return bool
            */
            bool IsComponentChanged(IN const CComponentBase& Component_) const
            {
                auto _pEntity = Component_.GetEntity();
                if (!_pEntity)
                {
                    return false;
                }
                auto _pDomain = _pEntity->GetDomain();
                if (!_pDomain)
                {
                    return false;
                }
                return IsComponentChanged(_pDomain->GetID(), _pEntity->GetID(), Component_.GetComponentClass());
            }

            /*
            * @brief 组件版本升级
            * @param [in] Component_
            */
            void BumpComponentVersion(IN const CComponentBase& Component_) const
            {
                auto _pEntity = Component_.GetEntity();
                if (!_pEntity)
                {
                    return;
                }
                auto _pDomain = _pEntity->GetDomain();
                if (!_pDomain)
                {
                    return;
                }
                return BumpComponentVersion(_pDomain->GetID(), _pEntity->GetID(), Component_.GetComponentClass());
            }

            /*
            * @brief 是否发生了更改
            * @param [in] nDomainID_
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            * @return bool
            */
            virtual bool IsComponentChanged(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const = 0;

            /*
            * @brief 获取组件版本
            * @param [in] nDomainID_
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            * @return size_t
            */
            virtual size_t GetComponentVersion(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const = 0;

            /*
            * @brief 组件版本升级
            * @param [in] nDomainID_
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            */
            virtual void BumpComponentVersion(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const = 0;

            /*
            * @brief 重置组件更改标记
            * @details
            *   上层调用，每一帧结束的时候调用一次
            */
            virtual void ResetComponentChangedFlag() = 0;
        };

        /*
        * @brief 生成数据仓储
        * @return  std::shared_ptr<IRepository>
        */
        std::shared_ptr<IRepository> _DATABASE_EXP GenerateRepository(IN const iCAX::Data::uuid& RepositoryID_);
    }
}