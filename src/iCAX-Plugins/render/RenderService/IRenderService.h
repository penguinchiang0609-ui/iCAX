#pragma once

#include "RenderSceneSnapshot.h"
#include "Data/uuid.h"
#include "Services/IService.h"

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

    namespace Render
    {
        /*
        * @brief 渲染服务接口。
        * @details
        *   RenderService 不假定具体渲染路线。PDO、OpenGL、Qt、WPF 都可以实现该接口。
        *   所有接口显式携带 ProjectID 和 SceneID，使同一服务实例也能隔离多个 project 和多个 viewport/preview/simulation scene。
        */
        class _RENDER_SERVICE_EXP IRenderService : public iCAX::Services::IService
        {
        public:
            ~IRenderService() override = default;

        public:
            /*
            * @brief 创建渲染场景。
            * @param [in] ProjectID_ 场景所属项目 ID。
            * @param [in] nSceneID_ 项目内渲染场景 ID，不能为 0。
            * @return true 表示创建成功；false 表示场景已存在。
            */
            virtual bool CreateScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_) = 0;

            /*
            * @brief 销毁指定渲染场景。
            * @return true 表示移除了已有场景。
            */
            virtual bool DestroyScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_) = 0;

            /*
            * @brief 销毁某个项目下的全部渲染场景。
            */
            virtual void DestroyProject(IN const iCAX::Data::uuid& ProjectID_) = 0;

            /*
            * @brief 判断场景是否存在。
            */
            virtual bool HasScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_) const = 0;

            /*
            * @brief 列出某个项目下的渲染场景 ID。
            */
            virtual std::vector<RenderSceneID> ListSceneIDs(IN const iCAX::Data::uuid& ProjectID_) const = 0;

            /*
            * @brief 刷新一帧渲染输出。
            * @param [in] ApplicationContext_ 应用级上下文。
            * @param [in] ProductContext_ 产品级上下文。
            * @param [in] ProjectContext_ 项目级上下文；项目级参数从这里进入。
            * @param [in,out] SceneContext_ 场景级上下文；PDO、资源和场景数据都从这里进入。
            * @param [in] nDeltaTime_ 当前帧距离上一帧的时间，单位秒。
            * @param [in] nTotalTime_ Scene 运行累计时间，单位秒。
            * @details
            *   Update 是 RenderService 的帧入口，也可以理解为渲染服务的 OnTick。
            *   PDO 实现应在这里把 RenderData 写入 Scene PDO；原生渲染实现应在这里刷新屏幕或提交渲染命令。
            */
            virtual void Update(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_) = 0;

            /*
            * @brief 清空场景内所有渲染数据，但保留场景本身。
            */
            virtual bool ClearScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_) = 0;

            virtual bool UpsertMesh(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_,
                IN const SRenderMeshData& Mesh_) = 0;

            virtual bool UpsertPolyline(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_,
                IN const SRenderPolylineData& Polyline_) = 0;

            virtual bool UpsertToolpath(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_,
                IN const SRenderToolpathData& Toolpath_) = 0;

            virtual bool RemoveGeometry(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_,
                IN RenderGeometryID nGeometryID_) = 0;

            virtual bool SetInstances(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_,
                IN const std::vector<SRenderInstanceData>& Instances_,
                IN const std::vector<SRenderStyleData>& Styles_,
                IN RenderDataVersion nDataVersion_) = 0;

            virtual bool SetTransforms(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_,
                IN const std::vector<SRenderTransformData>& Transforms_,
                IN RenderDataVersion nDataVersion_) = 0;

            virtual bool SetCameras(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_,
                IN const std::vector<SRenderCameraData>& Cameras_,
                IN RenderCameraID nActiveCameraID_,
                IN RenderDataVersion nDataVersion_) = 0;

            /*
            * @brief 获取场景快照。
            * @throws std::logic_error 场景不存在时抛出。
            */
            virtual SRenderSceneSnapshot GetSceneSnapshot(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_) const = 0;
        };
    }
}
