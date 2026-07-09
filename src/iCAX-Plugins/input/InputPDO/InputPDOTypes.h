#pragma once

#include "InputPDOExport.h"

#include <cstdint>

namespace iCAX
{
    namespace InputPDO
    {
        inline constexpr uint32_t kInputPDOMagic = 0x4F445049u; // "IPDO", little endian.
        inline constexpr uint32_t kInputPDOLayoutVersion = 1;

        using InputSceneID = uint64_t;
        using InputViewportID = uint64_t;
        using InputDeviceID = uint64_t;
        using InputDataVersion = uint64_t;
        using InputKeyCode = uint16_t;

        inline constexpr InputSceneID kInvalidInputSceneID = 0;
        inline constexpr InputViewportID kInvalidInputViewportID = 0;
        inline constexpr const char* kDefaultInputStatePDOInstanceName = "default";
        inline constexpr uint32_t kInputKeyBitCount = 512;
        inline constexpr uint32_t kInputKeyBitWordCount = kInputKeyBitCount / 64;

        enum class EInputPDOPayloadKind : uint32_t
        {
            Invalid = 0,
            State = 1
        };

        enum class EInputPointerKind : uint32_t
        {
            None = 0,
            Mouse = 1,
            Pen = 2,
            Touch = 3
        };

        enum class EInputKeyCode : InputKeyCode
        {
            Escape = 27,
            Space = 32,
            PageUp = 33,
            PageDown = 34,
            ArrowLeft = 37,
            ArrowUp = 38,
            ArrowRight = 39,
            ArrowDown = 40,
            Shift = 16,
            Ctrl = 17,
            Alt = 18,
            KeyA = 65,
            KeyD = 68,
            KeyE = 69,
            KeyQ = 81,
            KeyS = 83,
            KeyW = 87
        };

        inline constexpr uint32_t kInputStateFlagFocused = 1u << 0;
        inline constexpr uint32_t kInputStateFlagPointerInside = 1u << 1;
        inline constexpr uint32_t kInputStateFlagPointerCaptured = 1u << 2;
        inline constexpr uint32_t kInputStateFlagTextInputActive = 1u << 3;

        inline constexpr uint32_t kInputModifierShift = 1u << 0;
        inline constexpr uint32_t kInputModifierCtrl = 1u << 1;
        inline constexpr uint32_t kInputModifierAlt = 1u << 2;
        inline constexpr uint32_t kInputModifierSuper = 1u << 3;

        inline constexpr uint32_t kInputLockCaps = 1u << 0;
        inline constexpr uint32_t kInputLockNum = 1u << 1;
        inline constexpr uint32_t kInputLockScroll = 1u << 2;

        inline constexpr uint32_t kInputMouseButtonLeft = 1u << 0;
        inline constexpr uint32_t kInputMouseButtonRight = 1u << 1;
        inline constexpr uint32_t kInputMouseButtonMiddle = 1u << 2;
        inline constexpr uint32_t kInputMouseButtonX1 = 1u << 3;
        inline constexpr uint32_t kInputMouseButtonX2 = 1u << 4;
    }
}
