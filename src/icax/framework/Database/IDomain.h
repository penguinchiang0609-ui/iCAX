#pragma once
#include "Database.h"
#include "Data/uuid.h"
#include <map>
#include "IEntity.h"
#include <vector>
#include "IChecker.h"
#include "IDomainEvent.h"
#include "IEntitiesView.h"

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 域接口
        */
        class _DATABASE_EXP IDomain : public IDomianEventPublisher
        {
        public:
            IDomain() = default;
            virtual ~IDomain() = default;
            //!< 禁用
            //!< 
            IDomain(IN const IDomain&) = delete;
            IDomain(IN const IDomain&&) = delete;
            IDomain& operator=(IN const IDomain&) = delete;
            IDomain& operator=(IN const IDomain&&) = delete;

        public:
            /*
            * @brief 获取ID
            * @return iCAX::Data::uuid
            */
            virtual iCAX::Data::uuid GetID() const = 0;

        public:
            /*
            * @brief 创建实体
            * @param [in] ID_
            */
            virtual std::shared_ptr<IEntity> CreateEntity(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 是否包含实体
            * @param [in] ID_
            */
            virtual bool HasEntity(IN const iCAX::Data::uuid& ID_) const = 0;

            /*
            * @brief 移除实体
            * @param [in] ID_
            */
            virtual void DeleteEntity(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 获取实体
            * @param [in] ID_
            */
            virtual std::shared_ptr<IEntity> GetEntity(IN const iCAX::Data::uuid& ID_) const = 0;

            /*
            * @brief 筛选实体
            * @param [in] funcWhere_
            * @return std::vector<std::shared_ptr<CEntity>>
            */
            virtual std::vector<std::shared_ptr<IEntity>> FilterEntities(IN std::function<bool(std::shared_ptr<IEntity>)> funcWhere_) const = 0;

            /*
            * @brief 实体数量
            * @return int
            */
            virtual int EntityCount() const = 0;

            /*
            * @brief 获取实体的ID列表
            * @return std::vector<iCAX::Data::uuid>
            */
            virtual std::vector<iCAX::Data::uuid> GetEntityIDs() const = 0;

            /*
            * @brief 获取MetaEntity
            * @return std::shared_ptr<IEntity>
            * @remark 域自身的描述、配置信息承载者
            */
            virtual std::shared_ptr<IEntity> GetMetaEntity() const = 0;

            /*
            * @brief 获取实体视图
            * @return IEntitiesView&
            */
            virtual IEntitiesView& GetView() const = 0;

            /*
            * @brief 获取所在仓储
            * @return std::shared_ptr<class IRepository>
            */
            virtual std::shared_ptr<class IRepository> GetRepository() const = 0;

            /*
            * @brief 清空
            * @param [in] bForced_ 是否强制，默认false。如果为true，则本体实体也会被删除
            */
            virtual void CleanUp(IN const bool& bForced_ = false) = 0;

            /*
            * @brief 是否有效
            * @details
            *   如果缺失了本体实体，则为无效
            */
            virtual bool IsValid() = 0;

            /*
            * @brief 是否是持久化的
            * @details
            *   是否需要持久化。如零件编辑域为临时域，该方法应该返回false，在文件保存的时候无需持久化
            */
            virtual bool IsPersistent() = 0;
        };
    }
}
