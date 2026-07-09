#pragma once

#include "CommandHandler/CommandMessage.h"

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

    namespace CAM::Commands
    {
        /*
        * @brief 查询当前 CAM 场景摘要。
        */
        Command::CCommandResponse HandleGetScene(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 请求后端计算当前渲染场景的最佳视角。
        */
        Command::CCommandResponse HandleFitCameraView(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 导入机床 SDF/XML 描述文件。
        */
        Command::CCommandResponse HandleImportMachine(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改当前机床业务参数。
        */
        Command::CCommandResponse HandleSetMachineParameters(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改当前机床 TCP 参数。
        */
        Command::CCommandResponse HandleSetMachineTCP(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 按轴名点动当前机床。
        */
        Command::CCommandResponse HandleJogMachine(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 当前机床回零。
        */
        Command::CCommandResponse HandleHomeMachine(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 复位当前机床运行态。
        */
        Command::CCommandResponse HandleResetMachine(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 检查当前机床轴限位。
        */
        Command::CCommandResponse HandleCheckMachineLimits(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 检查当前机床是否具备规划入口条件。
        */
        Command::CCommandResponse HandleCheckMachineReach(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 导入工件模型资源并创建工件 Entity。
        */
        Command::CCommandResponse HandleImportModel(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置当前激活工件。
        */
        Command::CCommandResponse HandleSetActiveWorkpiece(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 写入当前拓扑选择。
        */
        Command::CCommandResponse HandlePickTopology(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 写入当前机床对象选择。
        */
        Command::CCommandResponse HandlePickMachineObject(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 从当前工件拓扑识别 loop 并生成刀路。
        */
        Command::CCommandResponse HandleRecognizeLoops(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 把当前拓扑选择转换成一条刀路。
        */
        Command::CCommandResponse HandleAddSelectionPath(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置指定刀路的姿态场采样。
        */
        Command::CCommandResponse HandleSetPoseField(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 清空当前 program。
        */
        Command::CCommandResponse HandleClearProgram(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);
    }
}
