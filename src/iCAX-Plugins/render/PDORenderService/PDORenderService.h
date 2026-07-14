#pragma once

#include "PDORenderServiceExport.h"
#include "CommandTargets/CommandRoute.h"
#include "RenderService/RenderService.h"
#include "PDO/IPDOSlot.h"
#include "Services/ServicesHelper.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace iCAX
{
    namespace PDORenderService
    {
        /*
        * @brief PDORenderService 发给前端的事件码。
        * @details
        *   事件通过 Scene mailbox 发送，payload 使用 UTF-8 JSON 文本。
        *   前端必须把 PDOID 当作 slot 身份，并在 shared memory arena 中按 PDOID 解析当前 offset，
        *   不允许长期缓存 offset。碎片整理或扩容后，同一个 PDOID 对应的 offset 可能变化。
        */
        inline constexpr uint64_t kPDORenderSlotAllocatedEvent =
            iCAX::Command::MakeCommandCode("PDORender", "SlotAllocated");
        inline constexpr uint64_t kPDORenderSlotFreedEvent =
            iCAX::Command::MakeCommandCode("PDORender", "SlotFreed");
        inline constexpr uint64_t kPDORenderSlotMovedEvent =
            iCAX::Command::MakeCommandCode("PDORender", "SlotMoved");
        inline constexpr uint64_t kPDORenderDefragBeginEvent =
            iCAX::Command::MakeCommandCode("PDORender", "DefragBegin");
        inline constexpr uint64_t kPDORenderDefragEndEvent =
            iCAX::Command::MakeCommandCode("PDORender", "DefragEnd");

        /*
        * @brief 基于 PDO 的 RenderService 实现。
        * @details
        *   该服务按 ProjectID + RenderSceneID 保存 RenderData。Update 时通过 SceneContext 获取当前 Scene PDOHub，
        *   按当前 RenderData 动态分配/释放 slot，并把数据写入目标 slot；服务不会长期持有 Scene PDOHub 指针。
        */
        class _PDO_RENDER_SERVICE_EXP CPDORenderService final : public iCAX::Render::IRenderService
        {
            AUTO_REGIST_SERVICE(iCAX::Render::IRenderService, CPDORenderService)

        private:
            struct SSlotAssignment final
            {
                iCAX::PDO::PDOID nPDOID = 0;
                uint32_t nSlotVersion = 0;
                uint64_t nPayloadCapacity = 0;
                bool bNeedPublishAllocatedEvent = false;
            };

            struct SScenePDOOutputState final
            {
                std::unordered_map<iCAX::Render::RenderGeometryID, SSlotAssignment> MeshSlots;
                std::unordered_map<iCAX::Render::RenderGeometryID, SSlotAssignment> PolylineSlots;
                std::unordered_map<iCAX::Render::RenderGeometryID, SSlotAssignment> ToolpathSlots;
                std::unordered_map<iCAX::Render::TransformID, SSlotAssignment> TransformSlots;
                std::unordered_map<iCAX::Render::SceneObjectID, SSlotAssignment> ObjectSlots;
                std::unordered_map<iCAX::Render::RenderCameraID, SSlotAssignment> CameraSlots;
            };

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

            void Update(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_) override;

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

            bool SetObjects(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN const std::vector<iCAX::Render::SRenderInstanceData>& Objects_) override;

            bool SetTransforms(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN const std::vector<iCAX::Render::STransformData>& Transforms_,
                IN iCAX::Render::RenderDataVersion nDataVersion_) override;

            bool SetCameras(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN const std::vector<iCAX::Render::SRenderCameraData>& Cameras_,
                IN iCAX::Render::RenderCameraID nActiveCameraID_) override;

            iCAX::Render::SRenderSceneSnapshot GetSceneSnapshot(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_) const override;

            /*
            * @brief 构造一个渲染几何 PDOID。
            * @details
            *   PDOID 是前后端共同使用的稳定身份。slot 在 arena 中移动或重新分配时，PDOID 不变。
            */
            static iCAX::PDO::PDOID MakeGeometryPDOID(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Render::ERenderGeometryKind eGeometryKind_,
                IN iCAX::Render::RenderGeometryID nGeometryID_);

            /*
            * @brief 构造一个渲染对象 PDOID。
            * @details 一个对象对应一个 Object PDO slot，payload 中只包含对象自己的 EntityID、GeometryID、MaterialID 和显示标志。
            */
            static iCAX::PDO::PDOID MakeObjectPDOID(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Render::SceneObjectID nObjectID_);

            /*
            * @brief 构造一个 Transform PDOID。
            * @details Transform 与其他组件按 ID 拼装；Transform payload 不知道自己属于哪个上层对象。
            */
            static iCAX::PDO::PDOID MakeTransformPDOID(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Render::TransformID nTransformID_);

            /*
            * @brief 构造一个相机 PDOID。
            * @details Camera PDO 由后端写、前端读；一个 camera entity 对应一个 PDO slot。
            */
            static iCAX::PDO::PDOID MakeCameraPDOID(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Render::RenderCameraID nCameraID_);

            /*
            * @brief 通知前端 PDO arena 即将开始碎片整理。
            * @details 前端收到后应暂停直接缓存 offset 的读写逻辑，只保留 PDOID，并等待 DefragEnd 后重新解析。
            */
            void NotifyPDODefragBegin(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Project::ISceneContext& SceneContext_) const;

            /*
            * @brief 通知前端某个 PDO slot 在 arena 中被移动。
            */
            void NotifyPDOSlotMoved(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN iCAX::PDO::PDOID nPDOID_) const;

            /*
            * @brief 通知前端 PDO arena 碎片整理结束。
            */
            void NotifyPDODefragEnd(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Project::ISceneContext& SceneContext_) const;

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

            bool WriteTransformToPDO(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Render::TransformID nTransformID_,
                IN iCAX::PDO::IPDOSlot& Slot_) const;

            /*
            * @brief 将单个渲染对象写入一个 Object PDO slot。
            */
            bool WriteObjectToPDO(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Render::SceneObjectID nObjectID_,
                IN iCAX::PDO::IPDOSlot& Slot_) const;

            bool WriteCameraToPDO(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Render::RenderCameraID nCameraID_,
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

            void SynchronizeScenePDOOutput(
                IN const iCAX::Data::uuid& ProjectID_,
                IN const iCAX::Render::SRenderSceneSnapshot* pScene_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN OUT SScenePDOOutputState& State_) const;

            void FreeScenePDOOutput(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN OUT SScenePDOOutputState& State_) const;

            void SendSlotEvent(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN uint64_t nEventTypeCode_,
                IN const char* pEventName_,
                IN const char* pSlotRole_,
                IN iCAX::Render::RenderSceneID nSceneID_,
                IN iCAX::PDO::PDOID nPDOID_,
                IN iCAX::Render::RenderGeometryID nGeometryID_,
                IN iCAX::Render::SceneObjectID nObjectID_,
                IN iCAX::Render::TransformID nTransformID_,
                IN const char* pPayloadKind_,
                IN uint32_t nSlotVersion_,
                IN uint64_t nPayloadCapacity_) const;

            void SendDefragEvent(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN uint64_t nEventTypeCode_,
                IN const char* pEventName_,
                IN iCAX::PDO::PDOID nPDOID_) const;

        private:
            mutable std::mutex m_Mutex;
            mutable std::atomic_uint64_t m_nNextEventMailID = 1;
            std::unordered_map<
                iCAX::Data::uuid,
                std::unordered_map<iCAX::Render::RenderSceneID, iCAX::Render::SRenderSceneSnapshot>> m_Projects;
            std::unordered_map<
                iCAX::Data::uuid,
                std::unordered_map<iCAX::Render::RenderSceneID, SScenePDOOutputState>> m_PDOOutputs;
        };
    }
}
