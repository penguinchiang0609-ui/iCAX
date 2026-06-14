#pragma once
#include "LogService.h"

#include <string>
#include "LogLevel.h"
#include <source_location>

namespace iCAX
{
    namespace LogService
    {
        /*
        * @brief 日志服务
        */
        class _LOGSERVICE_EXP ILogService
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
            * @brief 初始化
            * @param [in] strFileName_ 文件路径名
            * @param [in] nMaxFiles_ 最大文件数
            * @remark 此处只支持以天的方式，即每天生成一个日志文件
            */
            virtual void Initalize(IN const std::string& strFileName_, const int& nMaxFiles_) = 0;

            /*
            * @brief 设置日志级别
            * @param [in] nLevel_
            */
            virtual void SetLevel(IN const LogLevel& nLevel_) = 0;

            ///*
            //* @brief 设置格式
            //* @param [in] strFormat_
            //* @remark [%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%# %!][%^%t%$] %v
            //*   %Y-%m-%d %H:%M:%S.%e → 时间（精确到毫秒）
            //*   %l → 日志等级
            //*   %v → 日志内容（也就是你传给 Info/Debug 的 message）
            //*   %s → 文件名
            //*   %# → 行号
            //*   %! → 函数名
            //*   %t → 线程 id
            //*   %n → logger 名称
            //*   示例：[2025-09-15 22:59:00.123] [info] [main.cpp:42 main][1034] Hello world!
            //*/
            //virtual void SetFormat(IN const std::string& strFormat_) = 0;

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