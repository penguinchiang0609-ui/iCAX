#pragma once

#include "InputServiceExport.h"

#include "InputPDO/InputPDO.h"
#include "Data/uuid.h"
#include "Services/IService.h"

#include <array>
#include <cstdint>

namespace iCAX
{
    namespace Project
    {
        class IProjectContext;
        class ISceneContext;
    }

    namespace Input
    {
        struct _INPUT_SERVICE_EXP SInputPointerSnapshot final
        {
            uint32_t nPointerKind = static_cast<uint32_t>(iCAX::InputPDO::EInputPointerKind::Mouse);
            uint32_t nButtonMask = 0;
            uint32_t nFlags = 0;
            iCAX::InputPDO::InputDeviceID nDeviceID = 0;
            float nX = 0.0f;
            float nY = 0.0f;
            float nDeltaX = 0.0f;
            float nDeltaY = 0.0f;
            float nWheelX = 0.0f;
            float nWheelY = 0.0f;
        };

        struct _INPUT_SERVICE_EXP SInputFrameSnapshot final
        {
            bool bHasState = false;
            iCAX::InputPDO::InputViewportID nViewportID = iCAX::InputPDO::kInvalidInputViewportID;
            iCAX::InputPDO::InputDataVersion nDataVersion = 0;
            uint64_t nSequence = 0;
            uint64_t nTimestampUS = 0;
            uint32_t nStateFlags = 0;
            uint32_t nModifierMask = 0;
            uint32_t nLockMask = 0;
            SInputPointerSnapshot Pointer;
        };

        /*
        * @brief 后端输入状态服务。
        * @details
        *   InputService 从 Scene 的 input.state/default PDO 读取前端键鼠快照，
        *   并按 ProjectID + SceneID + ViewportID 保存状态。它只负责输入状态，不直接驱动业务。
        */
        class _INPUT_SERVICE_EXP IInputService : public iCAX::Services::IService
        {
        public:
            ~IInputService() override = default;

        public:
            /*
            * @brief 从当前 Scene 的 InputPDO 刷新输入状态。
            * @details
            *   应在 Scene 工作线程调用。该函数会确保默认 input.state slot 存在；
            *   如果本帧尚未收到前端输入，函数只保持已有状态。
            */
            virtual void UpdateFromPDO(
                IN const iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_) = 0;

            virtual bool GetKey(
                IN const iCAX::Data::uuid& ProjectID_,
                IN const iCAX::Data::uuid& SceneID_,
                IN iCAX::InputPDO::InputViewportID nViewportID_,
                IN iCAX::InputPDO::InputKeyCode nKeyCode_) const = 0;

            virtual bool GetKeyDown(
                IN const iCAX::Data::uuid& ProjectID_,
                IN const iCAX::Data::uuid& SceneID_,
                IN iCAX::InputPDO::InputViewportID nViewportID_,
                IN iCAX::InputPDO::InputKeyCode nKeyCode_) const = 0;

            virtual bool GetKeyUp(
                IN const iCAX::Data::uuid& ProjectID_,
                IN const iCAX::Data::uuid& SceneID_,
                IN iCAX::InputPDO::InputViewportID nViewportID_,
                IN iCAX::InputPDO::InputKeyCode nKeyCode_) const = 0;

            virtual SInputFrameSnapshot GetFrameSnapshot(
                IN const iCAX::Data::uuid& ProjectID_,
                IN const iCAX::Data::uuid& SceneID_,
                IN iCAX::InputPDO::InputViewportID nViewportID_) const = 0;
        };

        _INPUT_SERVICE_EXP uint32_t GetInputServiceContractVersion() noexcept;
    }
}

