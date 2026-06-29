#pragma once

#include "PDORenderServiceExport.h"
#include "RenderService/RenderService.h"
#include "PDO/IPDOSlot.h"
#include "Services/ServicesHelper.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace iCAX
{
    namespace PDORenderService
    {
        /*
        * @brief 基于 PDO 的 RenderService 实现。
        * @details
        *   该服务按 ProjectID + RenderSceneID 保存 RenderData。写 PDO 时，调用方显式传入目标 Slot，
        *   因此服务不会长期持有 Project PDOHub 指针，也不会在项目关闭后留下悬挂引用。
        */
        class _PDO_RENDER_SERVICE_EXP CPDORenderService final : public iCAX::Render::IRenderService
        {
            AUTO_REGIST_SERVICE(iCAX::Render::IRenderService, CPDORenderService)

        public:
            CPDORenderService();
            ~CPDORenderService() override;

            CPDORenderService(IN const CPDORenderService&) = delete;
            CPDORenderService& operator=(IN const CPDORenderService&) = delete;

        public:
            void OnLoad() override;
            void OnUnload() override;

            bool CreateScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_) override;

            bool DestroyScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_) override;

            void DestroyProject(IN const iCAX::Data::uuid& ProjectID_) override;

            bool HasScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_) const override;

            std::vector<iCAX::Render::RenderSceneID> ListSceneIDs(IN const iCAX::Data::uuid& ProjectID_) const override;

            bool ClearScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_) override;

            bool UpsertMesh(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN const iCAX::Render::SRenderMeshData& Mesh_) override;

            bool UpsertPolyline(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN const iCAX::Render::SRenderPolylineData& Polyline_) override;

            bool UpsertToolpath(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN const iCAX::Render::SRenderToolpathData& Toolpath_) override;

            bool RemoveGeometry(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Render::RenderGeometryID nGeometryID_) override;

            bool SetInstances(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN const std::vector<iCAX::Render::SRenderInstanceData>& Instances_,
                IN const std::vector<iCAX::Render::SRenderStyleData>& Styles_,
                IN iCAX::Render::RenderDataVersion nDataVersion_) override;

            bool SetViewStates(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN const std::vector<iCAX::Render::SRenderViewStateData>& Views_,
                IN uint32_t nActiveViewIndex_,
                IN iCAX::Render::RenderDataVersion nDataVersion_) override;

            iCAX::Render::SRenderSceneSnapshot GetSceneSnapshot(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_) const override;

            /*
            * @brief 将指定 mesh 写入一个 RenderPDO mesh slot。
            * @return true 表示本次写入完成；false 表示数据版本未更新或写缓冲暂不可用。
            */
            bool WriteMeshToPDO(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Render::RenderGeometryID nGeometryID_,
                IN iCAX::PDO::IPDOSlot& Slot_) const;

            bool WritePolylineToPDO(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Render::RenderGeometryID nGeometryID_,
                IN iCAX::PDO::IPDOSlot& Slot_) const;

            bool WriteToolpathToPDO(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Render::RenderGeometryID nGeometryID_,
                IN iCAX::PDO::IPDOSlot& Slot_) const;

            bool WriteInstanceListToPDO(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::PDO::IPDOSlot& Slot_) const;

            bool WriteViewStatesToPDO(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::PDO::IPDOSlot& Slot_) const;

        private:
            iCAX::Render::SRenderSceneSnapshot* FindSceneNoLock(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_);

            const iCAX::Render::SRenderSceneSnapshot* FindSceneNoLock(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_) const;

            static void ValidateProjectAndScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_);

        private:
            mutable std::mutex m_Mutex;
            std::unordered_map<
                iCAX::Data::uuid,
                std::unordered_map<iCAX::Render::RenderSceneID, iCAX::Render::SRenderSceneSnapshot>> m_Projects;
        };
    }
}
