#pragma once
#include "Database.h"
#include "IRepositoryEvent.h"
#include "ComponentMask.h"
#include <unordered_set>
#include "IComponentFrameCache.h"
#include <memory>
#include <set>


namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 组件帧缓存
        * @details
        *   监听 Repository 事件并维护组件类型到组件集合的缓存。
        *   当前帧缓存用于行为遍历，上一帧缓存用于判断组件是否是本帧新出现。
        */
        class _DATABASE_EXP CComponentFrameCache final
            : public IRepositoryEventListener
            , public IComponentFrameCache
        {
        public:
            CComponentFrameCache(IN std::shared_ptr<IRepository> pRepository_);
            ~CComponentFrameCache() override;

        public:
            /*
            * @brief 修改前事件
            * @param [in] pSender_
            * @param [in] Args_
            */
            void OnRepositoryChanging(IN void* pSender_, IN const RepositoryEventArgs& Args_) override;

            /*
            * @brief 更改后事件
            * @param [in] pSender_
            * @param [in] Args_
            */
            void OnRepositoryChanged(IN void* pSender_, IN const RepositoryEventArgs& Args_) override;

        public:
            /*
            * @brief 构建缓存
            * @param [in] strClassName_ 组件类名
            * @param [in] bForceReset_ 是否强制重建
            */
            void EnsureComponentCache(
                IN const std::string& strClassName_,
                IN const bool bForceReset_ = false) override;

            /*
            * @brief 获取缓存
            * @param [in] strClassName_ 组件类名
            * @return std::vector<std::shared_ptr<CComponentBase>>
            */
            std::vector<std::shared_ptr<CComponentBase>> GetCurrentComponents(
                IN const std::string& strClassName_) override;

            /*
            * @brief 获取前缓存
            * @param [in] strClassName_ 组件类名
            * @return std::vector<std::shared_ptr<CComponentBase>>
            */
            std::vector<std::shared_ptr<CComponentBase>> GetPreviousComponents(
                IN const std::string& strClassName_) override;

            /*
            * @brief 刷线前缓存
            */
            void CaptureCurrentAsPrevious() override;

        private:
            std::unordered_map<iCAX::Data::uuid, CComponentMask> m_EntityMask;
            std::unordered_map<size_t, std::set<std::weak_ptr<CComponentBase>, std::owner_less<std::weak_ptr<CComponentBase>>>> m_Cache;  //!< 缓存数据
            std::unordered_map<size_t, std::set<std::weak_ptr<CComponentBase>, std::owner_less<std::weak_ptr<CComponentBase>>>> m_PreCache;  //!< 缓存数据
            std::weak_ptr<IRepository> m_pRepository;
        };
    }
}
