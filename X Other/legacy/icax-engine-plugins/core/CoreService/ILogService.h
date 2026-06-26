#pragma once
#include "Core.h"
#include <string>
#include <source_location>
#include "Services/IService.h"

using namespace iCAX::Services;


namespace iCAX
{
    namespace Core
    {
        /*
        * @brief 日志等级
        */
        enum _CORE_EXP LogLevel
        {
            kTrace = 0,     //!< 最细的跟踪信息（调试内部细节）
            kDebug = 1,     //!< 调试信息
            kInfo = 2,      //!< 常规运行信息
            kWarn = 3,      //!< 警告（可能有问题，但程序还能跑）
            kError = 4,     //!< 错误（程序遇到错误，但未崩溃）
            kFault = 5,     //!< 严重错误（关键失败，可能导致退出）
            kOff = 6        //!< 关闭日志输出
        };

        /*
        * @brief 日志服务
        */
        class _CORE_EXP ILogService : public IService
        {
        public:
            /*
            * @brief 构造函数
            */
            ILogService() = default;

            /*
            * @brief 析构函数
            */
            virtual ~ILogService() = default;

        public:
            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Trace(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) = 0;

            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Debug(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) = 0;

            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Info(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) = 0;

            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Warn(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) = 0;

            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Error(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) = 0;

            /*
            * @brief 记录
            * @param [in] strMessage_
            * @param [in] Loc_ 用于携带文件名称、行号
            */
            virtual void Fault(IN const std::string& strMessage_, IN const std::source_location& Loc_ = std::source_location::current()) = 0;
        };
    }
}