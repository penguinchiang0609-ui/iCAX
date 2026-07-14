#include "pch.h"
#include "InputService.h"

#include "InputPDO/InputPDO.h"
#include "PDO/IPDOHub.h"
#include "PDO/PDOLease.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "Services/ServicesHelper.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace
{
    using KeyBits = std::array<uint64_t, iCAX::InputPDO::kInputKeyBitWordCount>;

    struct SceneKey final
    {
        iCAX::Data::uuid ProjectID;
        iCAX::Data::uuid SceneID;

        bool operator==(IN const SceneKey& Other_) const noexcept
        {
            return ProjectID == Other_.ProjectID && SceneID == Other_.SceneID;
        }
    };

    struct SceneKeyHasher final
    {
        size_t operator()(IN const SceneKey& Key_) const noexcept
        {
            const auto _A = std::hash<iCAX::Data::uuid>{}(Key_.ProjectID);
            const auto _B = std::hash<iCAX::Data::uuid>{}(Key_.SceneID);
            return _A ^ (_B + 0x9e3779b97f4a7c15ull + (_A << 6) + (_A >> 2));
        }
    };

    struct ViewportState final
    {
        bool bHasState = false;
        iCAX::InputPDO::InputDataVersion nLastProcessedDataVersion = 0;
        iCAX::InputPDO::SInputStatePDOHeader Current = {};
        KeyBits KeyDownThisFrame = {};
        KeyBits KeyUpThisFrame = {};
    };

    bool IsKeyBitSet(IN const KeyBits& Bits_, IN iCAX::InputPDO::InputKeyCode nKeyCode_) noexcept
    {
        if (nKeyCode_ >= iCAX::InputPDO::kInputKeyBitCount)
        {
            return false;
        }
        const uint32_t _WordIndex = nKeyCode_ / 64;
        const uint32_t _BitIndex = nKeyCode_ % 64;
        return (Bits_[_WordIndex] & (uint64_t{ 1 } << _BitIndex)) != 0;
    }

    KeyBits ToKeyBits(IN const iCAX::InputPDO::SInputKeyboardState& Keyboard_) noexcept
    {
        KeyBits _Bits = {};
        std::copy(Keyboard_.KeyDownBits.begin(), Keyboard_.KeyDownBits.end(), _Bits.begin());
        return _Bits;
    }

    KeyBits Difference(IN const KeyBits& Current_, IN const KeyBits& Previous_) noexcept
    {
        KeyBits _Result = {};
        for (size_t _Index = 0; _Index < _Result.size(); ++_Index)
        {
            _Result[_Index] = Current_[_Index] & ~Previous_[_Index];
        }
        return _Result;
    }

    void ClearOneFrameInput(IN OUT ViewportState& State_) noexcept
    {
        State_.KeyDownThisFrame = {};
        State_.KeyUpThisFrame = {};
        State_.Current.Pointer.nDeltaX = 0.0f;
        State_.Current.Pointer.nDeltaY = 0.0f;
        State_.Current.Pointer.nWheelX = 0.0f;
        State_.Current.Pointer.nWheelY = 0.0f;
    }

    iCAX::Input::SInputPointerSnapshot MakePointerSnapshot(IN const iCAX::InputPDO::SInputPointerState& Pointer_) noexcept
    {
        return {
            Pointer_.nPointerKind,
            Pointer_.nButtonMask,
            Pointer_.nFlags,
            Pointer_.nDeviceID,
            Pointer_.nX,
            Pointer_.nY,
            Pointer_.nDeltaX,
            Pointer_.nDeltaY,
            Pointer_.nWheelX,
            Pointer_.nWheelY
        };
    }

    class CInputService final : public iCAX::Input::IInputService
    {
        AUTO_REGIST_SERVICE(iCAX::Input::IInputService, CInputService)

    public:
        void OnLoad() override
        {
        }

        void OnUnload() override
        {
            std::lock_guard<std::mutex> _Lock(m_Mutex);
            m_Scenes.clear();
        }

        void UpdateFromPDO(
            IN const iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            if (!SceneContext_.HasPDOHub())
            {
                return;
            }

            const auto& _ProjectID = ProjectContext_.GetProjectID();
            const auto& _SceneID = SceneContext_.GetSceneID();
            if (_ProjectID.is_nil() || _SceneID.is_nil())
            {
                throw std::invalid_argument("InputService requires non-nil project and scene ids");
            }

            auto& _Hub = SceneContext_.PDOHub();
            const auto _Decl = iCAX::InputPDO::MakeInputStatePDODecl(iCAX::InputPDO::kDefaultInputStatePDOInstanceName);
            if (!_Hub.HasSlot(_Decl.nID))
            {
                (void)_Hub.AllocateSlot(_Decl);
            }

            auto& _Slot = _Hub.GetSlot(_Decl.nID);
            if (_Slot.GetPublishedDataVersion() == 0)
            {
                return;
            }

            iCAX::PDO::CPDOReadLease _Read(_Slot);
            if (_Read.DataVersion() == 0)
            {
                return;
            }

            const auto& _State = _Read.As<iCAX::InputPDO::SInputStatePDOHeader>();
            std::string _Error;
            if (!iCAX::InputPDO::ValidateInputStatePDOHeader(_State, static_cast<uint64_t>(_Slot.GetHeader().nPayloadSize), &_Error))
            {
                throw std::runtime_error("InputPDO state is invalid: " + _Error);
            }

            std::lock_guard<std::mutex> _Lock(m_Mutex);
            auto& _Viewport = m_Scenes[SceneKey{ _ProjectID, _SceneID }][_State.nViewportID];
            if (_Viewport.bHasState && _Viewport.nLastProcessedDataVersion == _State.Header.nDataVersion)
            {
                ClearOneFrameInput(_Viewport);
                return;
            }

            const auto _PreviousBits = _Viewport.bHasState ? ToKeyBits(_Viewport.Current.Keyboard) : KeyBits{};
            const auto _CurrentBits = ToKeyBits(_State.Keyboard);

            _Viewport.bHasState = true;
            _Viewport.nLastProcessedDataVersion = _State.Header.nDataVersion;
            _Viewport.Current = _State;
            _Viewport.KeyDownThisFrame = Difference(_CurrentBits, _PreviousBits);
            _Viewport.KeyUpThisFrame = Difference(_PreviousBits, _CurrentBits);
        }

        bool GetKey(
            IN const iCAX::Data::uuid& ProjectID_,
            IN const iCAX::Data::uuid& SceneID_,
            IN iCAX::InputPDO::InputViewportID nViewportID_,
            IN iCAX::InputPDO::InputKeyCode nKeyCode_) const override
        {
            std::lock_guard<std::mutex> _Lock(m_Mutex);
            const auto* _pState = FindViewportNoLock(ProjectID_, SceneID_, nViewportID_);
            return _pState != nullptr && iCAX::InputPDO::IsInputKeyDown(_pState->Current.Keyboard, nKeyCode_);
        }

        bool GetKeyDown(
            IN const iCAX::Data::uuid& ProjectID_,
            IN const iCAX::Data::uuid& SceneID_,
            IN iCAX::InputPDO::InputViewportID nViewportID_,
            IN iCAX::InputPDO::InputKeyCode nKeyCode_) const override
        {
            std::lock_guard<std::mutex> _Lock(m_Mutex);
            const auto* _pState = FindViewportNoLock(ProjectID_, SceneID_, nViewportID_);
            return _pState != nullptr && IsKeyBitSet(_pState->KeyDownThisFrame, nKeyCode_);
        }

        bool GetKeyUp(
            IN const iCAX::Data::uuid& ProjectID_,
            IN const iCAX::Data::uuid& SceneID_,
            IN iCAX::InputPDO::InputViewportID nViewportID_,
            IN iCAX::InputPDO::InputKeyCode nKeyCode_) const override
        {
            std::lock_guard<std::mutex> _Lock(m_Mutex);
            const auto* _pState = FindViewportNoLock(ProjectID_, SceneID_, nViewportID_);
            return _pState != nullptr && IsKeyBitSet(_pState->KeyUpThisFrame, nKeyCode_);
        }

        iCAX::Input::SInputFrameSnapshot GetFrameSnapshot(
            IN const iCAX::Data::uuid& ProjectID_,
            IN const iCAX::Data::uuid& SceneID_,
            IN iCAX::InputPDO::InputViewportID nViewportID_) const override
        {
            std::lock_guard<std::mutex> _Lock(m_Mutex);
            const auto* _pState = FindViewportNoLock(ProjectID_, SceneID_, nViewportID_);
            if (!_pState)
            {
                return {};
            }

            const auto& _Current = _pState->Current;
            iCAX::Input::SInputFrameSnapshot _Snapshot;
            _Snapshot.bHasState = _pState->bHasState;
            _Snapshot.nViewportID = _Current.nViewportID;
            _Snapshot.nDataVersion = _Current.Header.nDataVersion;
            _Snapshot.nSequence = _Current.nSequence;
            _Snapshot.nTimestampUS = _Current.nTimestampUS;
            _Snapshot.nStateFlags = _Current.nFlags;
            _Snapshot.nModifierMask = _Current.Keyboard.nModifierMask;
            _Snapshot.nLockMask = _Current.Keyboard.nLockMask;
            _Snapshot.Pointer = MakePointerSnapshot(_Current.Pointer);
            return _Snapshot;
        }

    private:
        const ViewportState* FindViewportNoLock(
            IN const iCAX::Data::uuid& ProjectID_,
            IN const iCAX::Data::uuid& SceneID_,
            IN iCAX::InputPDO::InputViewportID nViewportID_) const
        {
            auto _SceneIter = m_Scenes.find(SceneKey{ ProjectID_, SceneID_ });
            if (_SceneIter == m_Scenes.end())
            {
                return nullptr;
            }
            auto _ViewportIter = _SceneIter->second.find(nViewportID_);
            if (_ViewportIter == _SceneIter->second.end() || !_ViewportIter->second.bHasState)
            {
                return nullptr;
            }
            return &_ViewportIter->second;
        }

    private:
        mutable std::mutex m_Mutex;
        std::unordered_map<
            SceneKey,
            std::unordered_map<iCAX::InputPDO::InputViewportID, ViewportState>,
            SceneKeyHasher> m_Scenes;
    };
}

uint32_t iCAX::Input::GetInputServiceContractVersion() noexcept
{
    return 1;
}
