#pragma once

#include "CommandHandlerExport.h"

#include <cstdint>
#include <string>

namespace iCAX
{
    namespace Command
    {
        /*
        * @brief 计算命令名称的 32 位稳定码。
        * @details
        *   命令码用于进程内和跨语言协议中的底层路由。
        *   上层仍应以 ASCII 命令名称作为公开身份。
        */
        inline constexpr uint32_t CommandHash32(IN const char* pText_) noexcept
        {
            uint32_t _Hash = 2166136261u;
            while (*pText_)
            {
                _Hash ^= static_cast<uint8_t>(*pText_);
                _Hash *= 16777619u;
                ++pText_;
            }
            return _Hash;
        }

        /*
        * @brief 合成 64 位命令路由码。
        * @details 高 32 位是主命令码，低 32 位是子命令码。
        */
        inline constexpr uint64_t MakeCommandCode(IN uint32_t nMainCode_, IN uint32_t nSubCode_) noexcept
        {
            return (static_cast<uint64_t>(nMainCode_) << 32) | static_cast<uint64_t>(nSubCode_);
        }

        /*
        * @brief 根据主/子命令名生成路由码。
        */
        inline constexpr uint64_t MakeCommandCode(IN const char* pMainName_, IN const char* pSubName_) noexcept
        {
            return MakeCommandCode(CommandHash32(pMainName_), CommandHash32(pSubName_));
        }

        /*
        * @brief 从路由码中取主命令码。
        */
        inline constexpr uint32_t GetCommandMainCode(IN uint64_t nRouteCode_) noexcept
        {
            return static_cast<uint32_t>(nRouteCode_ >> 32);
        }

        /*
        * @brief 从路由码中取子命令码。
        */
        inline constexpr uint32_t GetCommandSubCode(IN uint64_t nRouteCode_) noexcept
        {
            return static_cast<uint32_t>(nRouteCode_ & 0xFFFFFFFFull);
        }

        /*
        * @brief 命令路由。
        * @details
        *   strMainName/strSubName 用于定义、诊断和文档表达。
        *   nMainCode/nSubCode 用于底层分发。跨语言传输时可以只传 64 位路由码。
        */
        struct _COMMAND_HANDLER_EXP CCommandRoute final
        {
            std::string strMainName; //!< 主命令名，如 App/Product/Project。
            std::string strSubName;  //!< 子命令名，如 GetState/OpenProjectCatalog。
            uint32_t nMainCode = 0;  //!< 主命令码。
            uint32_t nSubCode = 0;   //!< 子命令码。

            /*
            * @brief 获取合成后的 64 位路由码。
            */
            uint64_t GetRouteCode() const noexcept;

            /*
            * @brief 判断是否具备可分发的主/子命令码。
            */
            bool IsValid() const noexcept;
        };

        /*
        * @brief 运行时计算命令名称码。
        */
        _COMMAND_HANDLER_EXP uint32_t CommandHash32(IN const std::string& strText_);

        /*
        * @brief 判断单段命令名称是否合法。
        * @details
        *   单段命令名称用于 main 或 sub，必须匹配 [A-Z][A-Za-z0-9_]*。
        *   前端可使用 Main.Sub 组合命令，但 main/sub 本身不允许包含点号。
        */
        _COMMAND_HANDLER_EXP bool IsValidCommandName(IN const std::string& strName_);

        /*
        * @brief 根据主/子命令名创建完整路由。
        */
        _COMMAND_HANDLER_EXP CCommandRoute MakeCommandRoute(
            IN const std::string& strMainName_,
            IN const std::string& strSubName_);

        /*
        * @brief 根据 64 位路由码创建只有编码的路由。
        */
        _COMMAND_HANDLER_EXP CCommandRoute MakeCommandRoute(IN uint64_t nRouteCode_);

        /*
        * @brief 格式化命令路由，优先输出名称，名称缺失时输出编码。
        */
        _COMMAND_HANDLER_EXP std::string FormatCommandRoute(IN const CCommandRoute& Route_);
    }
}
