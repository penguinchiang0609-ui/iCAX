#pragma once
#include "Core.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <typeindex>
#include "Data/uuid.h"
#include "Services/IService.h"
#include "IResourceService.h"
#include "Services/ServicesHelper.h"
#include "ILogService.h"

namespace iCAX
{
    namespace Core
    {
        /*
        * @brief 资源池
        */
        class _CORE_EXP CResourceService final : public IResourceService
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] pLogService_
            */
            CResourceService(IN const std::shared_ptr<ILogService>& pLogService_);

            /*
            * @brief 析构函数
            */
            virtual ~CResourceService();

        public:
            /*
            * @brief 加载
            * @details
            *   服务需要的相关资源初始化，保证就绪
            */
            virtual void OnLoad() override;

            /*
            * @brief 卸载
            * @details
            *   释放服务占用的资源
            */
            virtual void OnUnload() override;

        public:
            /*
            * @brief 根据类型以及资源的KEY获取资源
            * @param [in] nType_
            * @param [in] strKey_
            * @return std::shared_ptr<void>
            */
            virtual std::shared_ptr<void> GetResource(IN const std::type_index& nType_, IN const std::string& strKey_) override;

            /*
            * @brief 根据类型以及资源的KEY设置资源
            * @param [in] nType_
            * @param [in] strKey_
            * @param [in] pResource_
            */
            virtual void SetResource(IN const std::type_index& nType_, IN const std::string& strKey_, IN const std::shared_ptr<void>& pResource_) override;

            /*
            * @brief 根据类型以及资源的KEY卸载资源
            * @param [in] nType_
            * @param [in] strKey_
            */
            virtual void DeleteResource(IN const std::type_index& nType_, IN const std::string& strKey_) override;

        private:
            std::shared_ptr<ILogService> m_pLogService;
            std::unordered_map<std::type_index, std::unordered_map<std::string, std::shared_ptr<void>>> m_mapResources;   //!< 资源映射表
        
            AUTO_REGIST_SERVICE(IResourceService, CResourceService, ILogService);
        };
    }
}
