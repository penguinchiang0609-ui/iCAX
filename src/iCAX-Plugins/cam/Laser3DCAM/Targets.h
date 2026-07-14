#pragma once

#include "Laser3DCAMExport.h"
#include "CommandTargets/CommandMessage.h"

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
        * @brief 请求后端计算当前渲染场景的最佳视角。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleFitCameraView(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 按标准方向切换当前相机视图。
        * @details 支持 front/right/top/iso 等常用视角；视图中心仍由后端根据当前渲染对象包围盒计算。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetStandardCameraView(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前场景中的机床实例列表。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleListMachines(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 按 Entity 查询机床元素当前属性。
        * @details 机床列表只返回结构树；右侧属性面板选中什么元素，就通过该命令查询该元素的 Transform、Joint、Link 等当前信息。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleGetMachineElement(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改机床元素相对父对象的局部 Transform。
        * @details 右侧属性面板编辑的是本地位置、姿态和缩放；父子关系和世界矩阵仍由后端 Transform 组件维护，前端只发命令。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetMachineElementTransform(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改机床元素的实例外观覆盖。
        * @details 只作用于当前项目实例中的一个 MachineElement；不会改机床定义资源，也不会影响子元素。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetMachineElementAppearance(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改机床实例中单根运动轴的软限位。
        * @details 该命令只修改当前实例展开后的 CMachineJointComponent，不回写 SDF/URDF 原始定义。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetMachineJointLimits(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 导入机床定义资源，不实例化到场景。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleImportMachineDefinition(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前项目中的机床定义列表。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleListMachineDefinitions(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置机床定义是否可用于实例化。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetMachineDefinitionEnabled(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 删除未被实例引用的机床定义。
        * @details 机床定义是项目中的资源索引；若已有机床实例引用该定义，删除会被拒绝，避免项目留下悬空实例。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleDeleteMachineDefinition(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 使用机床定义资源追加一个机床实例。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleInstantiateMachine(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改指定机床实例的业务参数。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetMachineParameters(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改指定机床实例的 TCP 参数。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetMachineTCP(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改指定机床元素上的工具 TCP。
        * @details TCP 挂在实际工具端元素上，输入和保存的都是相对该元素局部坐标系的数据；
        *   世界 TCP 由 Transform 派生矩阵计算并在查询 payload 中返回。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetMachineToolTCP(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 启用或禁用指定机床实例；禁用实例不会作为作业候选。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetMachineEnabled(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 按轴名点动指定机床实例。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleJogMachine(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 指定机床实例回零。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleHomeMachine(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 复位指定机床实例运行态。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleResetMachine(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 检查指定机床实例轴限位。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleCheckMachineLimits(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 检查指定机床实例是否具备规划入口条件。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleCheckMachineReach(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 导入工件模型资源，不创建工件 Entity。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleImportWorkpieceModel(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 使用工件模型资源实例化当前工件。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleInstantiateWorkpiece(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前场景中的工件、激活工件和拓扑拾取信息。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleListWorkpieces(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置当前激活工件。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetActiveWorkpiece(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 写入当前拓扑选择。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandlePickTopology(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前选择状态。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleGetSelection(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 写入机床对象选择。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandlePickMachineObject(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 从当前工件拓扑识别 loop 并生成刀路。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleRecognizeLoops(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询刀路、图层和程序结构。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleListToolpaths(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 把当前拓扑选择转换成一条刀路。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleAddSelectionPath(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置指定刀路的姿态场采样。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetPoseField(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 清空当前 program。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleClearProgram(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置作业使用的机床实例。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleSetJobMachine(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前作业状态。
        */
        _LASER_3D_CAM_EXP Command::CCommandResponse HandleGetJob(
            IN const Command::CCommandRequest& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);
    }
}
