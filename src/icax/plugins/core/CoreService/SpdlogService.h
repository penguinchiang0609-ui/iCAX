#pragma once
#include "Core.h"
#include "ILogService.h"
#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include "ISettingService.h"
#include "Services/ServicesHelper.h"

namespace iCAX
{
    namespace Core
    {
        /*
        * @brief 日志服务
        */
        class CSpdlogService : public ILogService
        {
        public:
            /*
            * @brief 构造函数
            */
            CSpdlogService(IN const std::shared_ptr<ISettingService>& pSetting_);

            /*
            * @brief 析构函数
            */
            virtual ~CSpdlogService();

        public://! IService成员
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

        public://! ILogService 成员
            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Trace(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) override;

            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Debug(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) override;

            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Info(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) override;

            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Warn(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) override;

            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Error(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) override;

            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Fault(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) override;

        private:
            std::shared_ptr<ISettingService> m_pSettingService;
            std::shared_ptr<spdlog::logger> m_pLogger;

            AUTO_REGIST_SERVICE(ILogService, CSpdlogService, ISettingService);
        };
    }
}