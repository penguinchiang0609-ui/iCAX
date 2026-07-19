#include "pch.h"


#include <InputPDO/InputPDO.h>
#include <PDO/IPDOHub.h>
#include <PDO/PDOLease.h>


using namespace iCAX::InputPDO;

namespace
{
    SInputStatePDOHeader MakeInputState(IN InputDataVersion nVersion_)
    {
        SInputStatePDOHeader _State;
        PrepareInputStatePDOHeader(_State, nVersion_);
        _State.nSceneID = 1;
        _State.nViewportID = 2;
        _State.nTimestampUS = 100;
        _State.nSequence = 7;
        _State.nFlags = kInputStateFlagFocused | kInputStateFlagPointerInside;
        _State.Keyboard.nModifierMask = kInputModifierShift | kInputModifierCtrl;
        _State.Pointer.nPointerKind = static_cast<uint32_t>(EInputPointerKind::Mouse);
        _State.Pointer.nDeviceID = 10;
        _State.Pointer.nX = 320.0f;
        _State.Pointer.nY = 240.0f;
        _State.Pointer.nDeltaX = 2.0f;
        _State.Pointer.nDeltaY = -1.0f;
        _State.Pointer.nWheelY = 120.0f;
        _State.Pointer.nButtonMask = kInputMouseButtonLeft;
        return _State;
    }
}

TEST(InputPDOLayoutTest, LayoutsAreBinarySafe)
{
    EXPECT_TRUE(std::is_standard_layout_v<SInputPDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SInputPDOHeader>);
    EXPECT_TRUE(std::is_standard_layout_v<SInputKeyboardState>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SInputKeyboardState>);
    EXPECT_TRUE(std::is_standard_layout_v<SInputPointerState>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SInputPointerState>);
    EXPECT_TRUE(std::is_standard_layout_v<SInputStatePDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SInputStatePDOHeader>);
    EXPECT_EQ(512u, kInputKeyBitCount);
}

TEST(InputPDODeclTest, CreatesInputStateDeclarationForInnerDirection)
{
    const auto _Decl = MakeInputStatePDODecl("main.viewport");

    EXPECT_EQ(kInputPDOLayoutVersion, _Decl.nVersion);
    EXPECT_NE(0u, _Decl.nID);
    EXPECT_EQ(iCAX::PDO::kDirection2Inner, _Decl.eDirection);
    EXPECT_EQ(sizeof(SInputStatePDOHeader), static_cast<size_t>(_Decl.nPayloadSize));
    EXPECT_STREQ("input.state", GetInputPDOPayloadTypeName(EInputPDOPayloadKind::State));

    EXPECT_THROW(MakeInputPDODecl(EInputPDOPayloadKind::Invalid, "main", iCAX::PDO::kDirection2Inner, sizeof(SInputStatePDOHeader)), std::invalid_argument);
    EXPECT_THROW(MakeInputPDODecl(EInputPDOPayloadKind::State, "", iCAX::PDO::kDirection2Inner, sizeof(SInputStatePDOHeader)), std::invalid_argument);
    EXPECT_THROW(MakeInputPDODecl(EInputPDOPayloadKind::State, "main", iCAX::PDO::kDirection2External, sizeof(SInputStatePDOHeader)), std::invalid_argument);
    EXPECT_THROW(MakeInputPDODecl(EInputPDOPayloadKind::State, "main", iCAX::PDO::kDirection2Inner, sizeof(SInputStatePDOHeader) - 1), std::invalid_argument);
}

TEST(InputPDOStateTest, PreparesFixedInputStateHeader)
{
    SInputStatePDOHeader _State;
    PrepareInputStatePDOHeader(_State, 12);

    EXPECT_EQ(kInputPDOMagic, _State.Header.nMagic);
    EXPECT_EQ(kInputPDOLayoutVersion, _State.Header.nLayoutVersion);
    EXPECT_EQ(static_cast<uint32_t>(EInputPDOPayloadKind::State), _State.Header.nPayloadKind);
    EXPECT_EQ(sizeof(SInputStatePDOHeader), _State.Header.nHeaderSize);
    EXPECT_EQ(sizeof(SInputStatePDOHeader), _State.Header.nPayloadSize);
    EXPECT_EQ(12u, _State.Header.nDataVersion);
}

TEST(InputPDOStateTest, ValidatesKeyboardAndPointerState)
{
    auto _State = MakeInputState(8);
    EXPECT_TRUE(SetInputKeyDown(_State.Keyboard, 65, true));
    EXPECT_TRUE(SetInputKeyDown(_State.Keyboard, 256, true));
    EXPECT_TRUE(SetInputKeyDown(_State.Keyboard, static_cast<InputKeyCode>(EInputKeyCode::ArrowUp), true));
    EXPECT_TRUE(SetInputKeyDown(_State.Keyboard, static_cast<InputKeyCode>(EInputKeyCode::PageDown), true));
    EXPECT_TRUE(IsInputKeyDown(_State.Keyboard, 65));
    EXPECT_TRUE(IsInputKeyDown(_State.Keyboard, 256));
    EXPECT_TRUE(IsInputKeyDown(_State.Keyboard, static_cast<InputKeyCode>(EInputKeyCode::ArrowUp)));
    EXPECT_TRUE(IsInputKeyDown(_State.Keyboard, static_cast<InputKeyCode>(EInputKeyCode::PageDown)));
    EXPECT_FALSE(IsInputKeyDown(_State.Keyboard, 66));
    EXPECT_FALSE(SetInputKeyDown(_State.Keyboard, 512, true));

    std::string _Error;
    EXPECT_TRUE(ValidateInputStatePDOHeader(_State, sizeof(SInputStatePDOHeader), &_Error));
    EXPECT_TRUE(_Error.empty());

    _State.Keyboard.nKeyBitCount = 128;
    EXPECT_FALSE(ValidateInputStatePDOHeader(_State, sizeof(SInputStatePDOHeader), &_Error));
    EXPECT_FALSE(_Error.empty());
}

TEST(InputPDOStateTest, WritesAndReadsInputSnapshotThroughPDOHub)
{
    auto _Hub = iCAX::PDO::GeneratePDOHub({ MakeInputStatePDODecl("main.viewport") });
    auto& _Slot = _Hub->GetSlot(iCAX::PDO::MakePDOID("input.state", "main.viewport"));

    auto _State = MakeInputState(9);
    ASSERT_TRUE(SetInputKeyDown(_State.Keyboard, 65, true));

    iCAX::PDO::CPDOWriteLease _Write(_Slot, _State.Header.nDataVersion);
    _Write.As<SInputStatePDOHeader>() = _State;
    _Write.Commit();

    _Hub->SwapInSlot();

    iCAX::PDO::CPDOReadLease _Read(_Slot);
    const auto& _ReadState = _Read.As<SInputStatePDOHeader>();
    EXPECT_TRUE(ValidateInputStatePDOHeader(_ReadState, sizeof(SInputStatePDOHeader)));
    EXPECT_EQ(1u, _ReadState.nSceneID);
    EXPECT_EQ(2u, _ReadState.nViewportID);
    EXPECT_EQ(7u, _ReadState.nSequence);
    EXPECT_TRUE(IsInputKeyDown(_ReadState.Keyboard, 65));
    EXPECT_EQ(kInputMouseButtonLeft, _ReadState.Pointer.nButtonMask);
    EXPECT_FLOAT_EQ(320.0f, _ReadState.Pointer.nX);
    EXPECT_FLOAT_EQ(240.0f, _ReadState.Pointer.nY);
}
