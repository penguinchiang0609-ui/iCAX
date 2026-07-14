#pragma once

#include "CommandRegistry.h"

namespace iCAX
{
    namespace Command
    {
        /*
        * @brief 命令分发器。
        * @details
        *   Dispatcher 根据 Request.Route.nMainCode 查找命令目标。
        *   具体子命令由命令目标根据 Request.Route.nSubCode 继续分发。
        *   它只处理路由层可以直接判断的失败；handler 抛出的异常会原样向外传播。
        */
        class _COMMAND_TARGETS_EXP CCommandDispatcher final
        {
        public:
            /*
            * @brief 构造分发器。
            * @param [in] pRegistry_ 命令注册表，不能为空。
            */
            explicit CCommandDispatcher(IN std::shared_ptr<CCommandRegistry> pRegistry_);
            ~CCommandDispatcher() = default;

            CCommandDispatcher(IN const CCommandDispatcher&) = delete;
            CCommandDispatcher& operator=(IN const CCommandDispatcher&) = delete;

        public:
            /*
            * @brief 分发命令。
            * @param [in] Request_ 命令请求。
            * @param [in] ApplicationContext_ 应用上下文。
            * @param [in] pProductContext_ 产品上下文；应用级命令为空。
            * @param [in] pProjectContext_ 项目上下文；非项目命令为空。
            * @param [in] pSceneContext_ 场景上下文；非场景命令为空。
            * @return 命令响应；路由无效返回 InvalidRequest，找不到 handler 返回 NoHandler。
            * @throws handler 抛出的异常会原样向外传播。
            */
            CCommandResponse Dispatch(
                IN const CCommandRequest& Request_,
                IN iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_) const;

        private:
            std::shared_ptr<CCommandRegistry> m_pRegistry;
        };
    }
}
