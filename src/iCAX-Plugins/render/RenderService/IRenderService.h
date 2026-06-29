#pragma once

#include "RenderSceneSnapshot.h"
#include "Data/uuid.h"
#include "Services/IService.h"

#include <vector>

namespace iCAX
{
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

            virtual bool SetViewStates(
                IN const iCAX::Data::uuid& ProjectID_,
                IN RenderSceneID nSceneID_,
                IN const std::vector<SRenderViewStateData>& Views_,
                IN uint32_t nActiveViewIndex_,
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
