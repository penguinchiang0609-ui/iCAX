#pragma once

#include "Laser3DCAMExport.h"
#include "Facades/Invocation.h"

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

    namespace CAM::Facades
    {
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleBeginWorkpieceEdit(
            IN const Interaction::CInvocation&, IN const Application::IApplicationContext&, IN Product::IProductContext*, IN Project::IProjectContext*, IN Project::ISceneContext*);
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleInspectWorkpieceEdit(
            IN const Interaction::CInvocation&, IN const Application::IApplicationContext&, IN Product::IProductContext*, IN Project::IProjectContext*, IN Project::ISceneContext*);
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleCommitWorkpieceEdit(
            IN const Interaction::CInvocation&, IN const Application::IApplicationContext&, IN Product::IProductContext*, IN Project::IProjectContext*, IN Project::ISceneContext*);
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleDiscardWorkpieceEdit(
            IN const Interaction::CInvocation&, IN const Application::IApplicationContext&, IN Product::IProductContext*, IN Project::IProjectContext*, IN Project::ISceneContext*);
        /*
        * @brief 请求后端计算当前渲染场景的最佳视角。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleFitCameraView(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 按标准方向切换当前相机视图。
        * @details 支持 front/right/top/iso 等常用视角；视图中心仍由后端根据当前渲染对象包围盒计算。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetStandardCameraView(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前场景中的机床实例列表。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleListMachines(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 按 Entity 查询机床元素当前属性。
        * @details 机床列表只返回结构树；右侧属性面板选中什么元素，就通过该方法查询该元素的 Transform、Joint、Link 等当前信息。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleGetMachineElement(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改机床元素相对父对象的局部 Transform。
        * @details 右侧属性面板编辑的是本地位置、姿态和缩放；父子关系和世界矩阵仍由后端 Transform 组件维护，前端只调用 Facade 方法。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetMachineElementTransform(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改机床元素的实例外观覆盖。
        * @details 只作用于当前项目实例中的一个 MachineElement；不会改机床定义源文件，也不会影响子元素。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetMachineElementAppearance(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置机床实例中单根运动轴的当前位置。
        * @details Position 是关节唯一的运动自由度；关节原点 Transform 属于结构定义，不通过该方法修改。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetMachineJointPosition(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改机床实例中单根运动轴的软限位。
        * @details 该方法只修改当前实例展开后的 CMachineJointComponent，不回写 SDF/URDF 原始定义。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetMachineJointLimits(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 导入产品级机床定义。
        * @details 这里只托管源文件并写入 ProductSettings，不解析 SDF/URDF，也不写入项目 Repository。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleImportMachineDefinition(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询产品级机床定义列表。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleListMachineDefinitions(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前产品支持的机床定义文件格式。
        * @details 这是产品静态能力查询，不返回项目或场景里的机床定义列表。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleGetSupportedMachineDefinitionFormats(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置机床定义是否可用于实例化。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetMachineDefinitionEnabled(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置产品默认机床定义。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetDefaultMachineDefinition(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 删除产品级机床定义。
        * @details 已经实例化到项目中的机床必须自洽；删除产品级定义不影响已有项目实例。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleDeleteMachineDefinition(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 使用产品级机床定义追加一个机床实例。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleInstantiateMachine(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改指定机床实例的业务参数。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetMachineParameters(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改指定机床实例的 TCP 参数。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetMachineTCP(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改指定机床元素上的工具 TCP。
        * @details TCP 挂在实际工具端元素上，输入和保存的都是相对该元素局部坐标系的数据；
        *   世界 TCP 由 Transform 派生矩阵计算并在查询 payload 中返回。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetMachineToolTCP(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 启用或禁用指定机床实例；禁用实例不会作为作业候选。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetMachineEnabled(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改指定机床实例的显示名称。
        * @details 该名称属于当前项目中的机床实例，不回写机床定义源文件。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetMachineName(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 按轴名点动指定机床实例。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleJogMachine(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 指定机床实例回零。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleHomeMachine(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 复位指定机床实例运行态。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleResetMachine(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 检查指定机床实例轴限位。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleCheckMachineLimits(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 检查指定机床实例是否具备规划入口条件。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleCheckMachineReach(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 导入工件模型资源，不创建工件 Entity。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleImportWorkpieceModel(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 使用工件模型资源实例化当前工件。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleInstantiateWorkpiece(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前场景中的工件、激活工件和拓扑拾取信息。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleListWorkpieces(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置当前激活工件。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetActiveWorkpiece(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 写入当前拓扑选择。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandlePickTopology(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前选择状态。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleGetSelection(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 写入机床对象选择。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandlePickMachineObject(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 从当前工件拓扑识别 loop 并生成刀路。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleRecognizeLoops(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询刀路、图层和程序结构。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleListToolpaths(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 把当前拓扑选择转换成一条刀路。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleAddSelectionPath(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置指定刀路的姿态场采样。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetPoseField(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 清空当前 program。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleClearProgram(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置作业使用的机床实例。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleSetJobMachine(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前作业状态。
        */
        _LASER_3D_CAM_EXP Interaction::CInvocationResult HandleGetJob(
            IN const Interaction::CInvocation& Request_,
            IN const Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);
    }
}
