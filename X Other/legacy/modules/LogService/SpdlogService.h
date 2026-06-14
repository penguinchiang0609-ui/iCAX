#pragma once
#include "LogService.h"
#include "ILogService.h"
#include <string>
#include "LogLevel.h"
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>

namespace iCAX
{
    namespace LogService
    {
        /*
        * @brief 日志服务
        */
        class SpdlogService : public ILogService
        {
        public:
            /*
            * @brief 构造函数
            */
            SpdlogService();

            /*
            * @brief 析构函数
            */
            virtual ~SpdlogService();

        public:
            /*
            * @brief 初始化
            * @param [in] strFileName_ 文件路径名
            * @param [in] nMaxFiles_ 最大文件数
            * @remark 此处只支持以天的方式，即每天生成一个日志文件
            */
            virtual void Initalize(IN const std::string& strFileName_, const int& nMaxFiles_) override;

            /*
            * @brief 设置日志级别
            * @param [in] nLevel_
            */
            virtual void SetLevel(IN const LogLevel& nLevel_) override;

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
            std::shared_ptr<spdlog::logger> m_pLogger;
        };
    }
}