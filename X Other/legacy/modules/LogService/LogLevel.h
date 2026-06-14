#pragma once
#include "LogService.h"


namespace iCAX
{
    namespace LogService
    {
        /*
        * @brief 日志等级
        */
        enum _LOGSERVICE_EXP LogLevel
        {
            kTrace = 0,     //!< 最细的跟踪信息（调试内部细节）
            kDebug = 1,     //!< 调试信息
            kInfo = 2,      //!< 常规运行信息
            kWarn = 3,      //!< 警告（可能有问题，但程序还能跑）
            kError = 4,     //!< 错误（程序遇到错误，但未崩溃）
            kFault = 5,     //!< 严重错误（关键失败，可能导致退出）
            kOff = 6        //!< 关闭日志输出
        };
    }
}