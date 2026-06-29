#pragma once

#include "InputPDOTypes.h"

#include <array>
#include <type_traits>

namespace iCAX
{
    namespace InputPDO
    {
        /*
        * @brief 所有 Input PDO payload 的公共头。
        * @details
        *   InputPDO 当前只定义固定大小的 input.state。nDataVersion 由前端逐次递增，
        *   后端可以用它跳过未变化的输入快照。
        */
        struct _INPUT_PDO_EXP SInputPDOHeader final
        {
            uint32_t nMagic = kInputPDOMagic;
            uint32_t nLayoutVersion = kInputPDOLayoutVersion;
            uint32_t nPayloadKind = static_cast<uint32_t>(EInputPDOPayloadKind::Invalid);
            uint32_t nHeaderSize = sizeof(SInputPDOHeader);
            uint64_t nPayloadSize = sizeof(SInputPDOHeader);
            InputDataVersion nDataVersion = 0;
        };

        /*
        * @brief 键盘当前状态。
        * @details
        *   KeyDownBits 使用 0..511 的稳定 key code 位图。具体 UI 框架负责把平台 key code
        *   映射到产品约定的 InputKeyCode；后端只读取位图状态。
        */
        struct _INPUT_PDO_EXP SInputKeyboardState final
        {
            uint32_t nModifierMask = 0;
            uint32_t nLockMask = 0;
            uint32_t nKeyBitCount = kInputKeyBitCount;
            uint32_t nReserved = 0;
            std::array<uint64_t, kInputKeyBitWordCount> KeyDownBits = {};
        };

        /*
        * @brief 指针当前状态。
        * @details
        *   鼠标位置采用 viewport 局部像素坐标；delta 和 wheel 表达自上一快照以来前端累计的变化量。
        */
        struct _INPUT_PDO_EXP SInputPointerState final
        {
            uint32_t nPointerKind = static_cast<uint32_t>(EInputPointerKind::Mouse);
            uint32_t nButtonMask = 0;
            uint32_t nFlags = 0;
            uint32_t nReserved = 0;
            InputDeviceID nDeviceID = 0;
            float nX = 0.0f;
            float nY = 0.0f;
            float nDeltaX = 0.0f;
            float nDeltaY = 0.0f;
            float nWheelX = 0.0f;
            float nWheelY = 0.0f;
        };

        /*
        * @brief 输入状态 PDO payload。
        * @details
        *   ProjectID 不放入 payload，因为 PDOHub 本身归属具体 project。SceneID 和 ViewportID
        *   用于后端把输入路由到具体场景和视口。
        */
        struct _INPUT_PDO_EXP SInputStatePDOHeader final
        {
            SInputPDOHeader Header;
            InputSceneID nSceneID = kInvalidInputSceneID;
            InputViewportID nViewportID = kInvalidInputViewportID;
            uint64_t nTimestampUS = 0;
            uint64_t nSequence = 0;
            uint32_t nFlags = 0;
            uint32_t nReserved = 0;
            SInputKeyboardState Keyboard;
            SInputPointerState Pointer;
        };

        /*
        * @brief 填充 input.state 的固定头字段。
        * @details
        *   PDO payload 需要保持 trivially-copyable，因此这里不通过构造函数修改布局语义。
        *   前端写入快照时先调用本函数，再填写 scene、viewport、键盘和指针状态。
        */
        inline void PrepareInputStatePDOHeader(IN OUT SInputStatePDOHeader& State_, IN InputDataVersion nDataVersion_) noexcept
        {
            State_.Header.nMagic = kInputPDOMagic;
            State_.Header.nLayoutVersion = kInputPDOLayoutVersion;
            State_.Header.nPayloadKind = static_cast<uint32_t>(EInputPDOPayloadKind::State);
            State_.Header.nHeaderSize = sizeof(SInputStatePDOHeader);
            State_.Header.nPayloadSize = sizeof(SInputStatePDOHeader);
            State_.Header.nDataVersion = nDataVersion_;
        }

        inline bool IsInputKeyDown(IN const SInputKeyboardState& State_, IN InputKeyCode nKeyCode_) noexcept
        {
            if (nKeyCode_ >= kInputKeyBitCount)
            {
                return false;
            }
            const uint32_t _WordIndex = nKeyCode_ / 64;
            const uint32_t _BitIndex = nKeyCode_ % 64;
            return (State_.KeyDownBits[_WordIndex] & (uint64_t{ 1 } << _BitIndex)) != 0;
        }

        inline bool SetInputKeyDown(IN OUT SInputKeyboardState& State_, IN InputKeyCode nKeyCode_, IN bool bDown_) noexcept
        {
            if (nKeyCode_ >= kInputKeyBitCount)
            {
                return false;
            }
            const uint32_t _WordIndex = nKeyCode_ / 64;
            const uint32_t _BitIndex = nKeyCode_ % 64;
            const uint64_t _Mask = uint64_t{ 1 } << _BitIndex;
            if (bDown_)
            {
                State_.KeyDownBits[_WordIndex] |= _Mask;
            }
            else
            {
                State_.KeyDownBits[_WordIndex] &= ~_Mask;
            }
            return true;
        }

        static_assert(kInputKeyBitCount % 64 == 0);
        static_assert(std::is_standard_layout_v<SInputPDOHeader>);
        static_assert(std::is_trivially_copyable_v<SInputPDOHeader>);
        static_assert(std::is_standard_layout_v<SInputKeyboardState>);
        static_assert(std::is_trivially_copyable_v<SInputKeyboardState>);
        static_assert(std::is_standard_layout_v<SInputPointerState>);
        static_assert(std::is_trivially_copyable_v<SInputPointerState>);
        static_assert(std::is_standard_layout_v<SInputStatePDOHeader>);
        static_assert(std::is_trivially_copyable_v<SInputStatePDOHeader>);
    }
}
