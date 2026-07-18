#pragma once

#include "Laser3DCAMExport.h"
#include "Facades/FacadeCall.h"

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
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleBeginWorkpieceEdit(
            IN const Interaction::CFacadeCall&, IN Application::IApplicationContext&, IN Product::IProductContext*, IN Project::IProjectContext*, IN Project::ISceneContext*);
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleInspectWorkpieceEdit(
            IN const Interaction::CFacadeCall&, IN Application::IApplicationContext&, IN Product::IProductContext*, IN Project::IProjectContext*, IN Project::ISceneContext*);
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleCommitWorkpieceEdit(
            IN const Interaction::CFacadeCall&, IN Application::IApplicationContext&, IN Product::IProductContext*, IN Project::IProjectContext*, IN Project::ISceneContext*);
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleDiscardWorkpieceEdit(
            IN const Interaction::CFacadeCall&, IN Application::IApplicationContext&, IN Product::IProductContext*, IN Project::IProjectContext*, IN Project::ISceneContext*);
        /*
        * @brief 请求后端计算当前渲染场景的最佳视角。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleFitCameraView(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 按标准方向切换当前相机视图。
        * @details 支持 front/right/top/iso 等常用视角；视图中心仍由后端根据当前渲染对象包围盒计算。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetStandardCameraView(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前场景中的机床实例列表。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleListMachines(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 按 Entity 查询机床元素当前属性。
        * @details 机床列表只返回结构树；右侧属性面板选中什么元素，就通过该方法查询该元素的 Transform、Joint、Link 等当前信息。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleGetMachineElement(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改机床元素相对父对象的局部 Transform。
        * @details 右侧属性面板编辑的是本地位置、姿态和缩放；父子关系和世界矩阵仍由后端 Transform 组件维护，前端只调用 Facade 方法。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetMachineElementTransform(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改机床元素的实例外观覆盖。
        * @details 只作用于当前项目实例中的一个 MachineElement；不会改机床定义源文件，也不会影响子元素。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetMachineElementAppearance(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改机床实例中单根运动轴的软限位。
        * @details 该方法只修改当前实例展开后的 CMachineJointComponent，不回写 SDF/URDF 原始定义。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetMachineJointLimits(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 导入产品级机床定义。
        * @details 这里只托管源文件并写入 ProductSettings，不解析 SDF/URDF，也不写入项目 Repository。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleImportMachineDefinition(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询产品级机床定义列表。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleListMachineDefinitions(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前产品支持的机床定义文件格式。
        * @details 这是产品静态能力查询，不返回项目或场景里的机床定义列表。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleGetSupportedMachineDefinitionFormats(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置机床定义是否可用于实例化。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetMachineDefinitionEnabled(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置产品默认机床定义。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetDefaultMachineDefinition(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 删除产品级机床定义。
        * @details 已经实例化到项目中的机床必须自洽；删除产品级定义不影响已有项目实例。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleDeleteMachineDefinition(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 使用产品级机床定义追加一个机床实例。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleInstantiateMachine(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改指定机床实例的业务参数。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetMachineParameters(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改指定机床实例的 TCP 参数。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetMachineTCP(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改指定机床元素上的工具 TCP。
        * @details TCP 挂在实际工具端元素上，输入和保存的都是相对该元素局部坐标系的数据；
        *   世界 TCP 由 Transform 派生矩阵计算并在查询 payload 中返回。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetMachineToolTCP(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 启用或禁用指定机床实例；禁用实例不会作为作业候选。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetMachineEnabled(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 修改指定机床实例的显示名称。
        * @details 该名称属于当前项目中的机床实例，不回写机床定义源文件。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetMachineName(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 按轴名点动指定机床实例。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleJogMachine(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 指定机床实例回零。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleHomeMachine(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 复位指定机床实例运行态。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleResetMachine(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 检查指定机床实例轴限位。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleCheckMachineLimits(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 检查指定机床实例是否具备规划入口条件。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleCheckMachineReach(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 导入工件模型资源，不创建工件 Entity。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleImportWorkpieceModel(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 使用工件模型资源实例化当前工件。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleInstantiateWorkpiece(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前场景中的工件、激活工件和拓扑拾取信息。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleListWorkpieces(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置当前激活工件。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetActiveWorkpiece(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 写入当前拓扑选择。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandlePickTopology(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前选择状态。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleGetSelection(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 写入机床对象选择。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandlePickMachineObject(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 从当前工件拓扑识别 loop 并生成刀路。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleRecognizeLoops(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询刀路、图层和程序结构。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleListToolpaths(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 把当前拓扑选择转换成一条刀路。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleAddSelectionPath(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置指定刀路的姿态场采样。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetPoseField(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 清空当前 program。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleClearProgram(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 设置作业使用的机床实例。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleSetJobMachine(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);

        /*
        * @brief 查询当前作业状态。
        */
        _LASER_3D_CAM_EXP Interaction::CFacadeResult HandleGetJob(
            IN const Interaction::CFacadeCall& Request_,
            IN Application::IApplicationContext& ApplicationContext_,
            IN Product::IProductContext* pProductContext_,
            IN Project::IProjectContext* pProjectContext_,
            IN Project::ISceneContext* pSceneContext_);
    }
}
