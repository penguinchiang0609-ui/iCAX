#pragma once

#include "CommandMessage.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Application
    {
        class IApplicationContext;
    }

    namespace Product
    {
        class IProductContext;
    }

    namespace Project
    {
        class IProjectContext;
        class ISceneContext;
    }

    namespace Command
    {
        /*
        * @brief 命令目标接口。
        * @details
        *   一个命令目标对应一个主命令，如 App/Product/Project。
        *   命令目标内部再根据子命令分发到具体函数，避免一个命令一个类。
        */
        class _COMMAND_TARGETS_EXP ICommandTarget
        {
        public:
            ICommandTarget() = default;
            virtual ~ICommandTarget() = default;

            ICommandTarget(IN const ICommandTarget&) = delete;
            ICommandTarget& operator=(IN const ICommandTarget&) = delete;

        public:
            /*
            * @brief 获取主命令名。
            */
            virtual const std::string& GetMainName() const = 0;

            /*
            * @brief 获取主命令码。
            */
            virtual uint32_t GetMainCode() const = 0;

            /*
            * @brief 判断是否存在指定子命令。
            */
            virtual bool HasSubCommand(IN uint32_t nSubCode_) const = 0;

            /*
            * @brief 获取本命令目标已注册的子命令路由。
            */
            virtual std::vector<CCommandRoute> GetSubCommandRoutes() const = 0;

            /*
            * @brief 执行请求中的子命令。
            */
            virtual CCommandResponse Handle(
                IN const CCommandRequest& Request_,
                IN iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_) = 0;
        };

        /*
            * @brief 命令目标基类。
            * @details
            *   业务侧应继承 CCommandTarget，实现一个具体主命令类；
            *   派生类在构造函数中调用 Bind() 绑定自己的子命令函数。
            *   Bind() 不对外公开，避免注册后从外部继续修改命令协议。
            */
        class _COMMAND_TARGETS_EXP CCommandTarget : public ICommandTarget
        {
        public:
            using SubCommandFunc = std::function<CCommandResponse(
                const CCommandRequest&,
                iCAX::Application::IApplicationContext&,
                iCAX::Product::IProductContext*,
                iCAX::Project::IProjectContext*,
                iCAX::Project::ISceneContext*)>;

        protected:
            /*
            * @brief 构造命令目标基类。
            * @param [in] strMainName_ 主命令名，不能为空。
            */
            explicit CCommandTarget(IN std::string strMainName_);

        public:
            ~CCommandTarget() override = default;

            CCommandTarget(IN const CCommandTarget&) = delete;
            CCommandTarget& operator=(IN const CCommandTarget&) = delete;

        public:
            const std::string& GetMainName() const override;
            uint32_t GetMainCode() const override;
            bool HasSubCommand(IN uint32_t nSubCode_) const override;
            std::vector<CCommandRoute> GetSubCommandRoutes() const override;

            CCommandResponse Handle(
                IN const CCommandRequest& Request_,
                IN iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_) override;

        protected:
            /*
            * @brief 注册子命令。
            * @return true 表示新增成功；false 表示同一子命令已存在。
            * @throws std::invalid_argument 子命令名为空或函数为空。
            * @throws std::runtime_error 子命令码与不同名称发生碰撞。
            * @details 命令目标不提供替换接口；同一 runtime 内命令协议应保持稳定。
            */
            bool Bind(IN std::string strSubName_, IN SubCommandFunc Func_);

        private:
            struct SubCommandRecord final
            {
                CCommandRoute Route;
                SubCommandFunc Func;
            };

        private:
            bool RegisterSubCommand(
                IN std::string strSubName_,
                IN SubCommandFunc Func_);

            static void ValidateSubCommandFunc(IN const SubCommandFunc& Func_);

        private:
            std::string m_strMainName;
            uint32_t m_nMainCode = 0;
            mutable std::mutex m_Mutex;
            std::map<uint32_t, SubCommandRecord> m_mapSubCommands;
        };
    }
}
