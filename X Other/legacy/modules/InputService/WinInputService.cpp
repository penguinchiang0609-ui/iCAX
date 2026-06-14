#include "pch.h"
#include "WinInputService.h"
using namespace iCAX::Services;

const static KeyCode s_AllKeys[] =
{
    //!< 控制键
    kCodeEscape, kCodeTab, kCodeCapsLock, kCodeShiftLeft, kCodeShiftRight, kCodeCtrlLeft, kCodeCtrlRight,
    kCodeAltLeft, kCodeAltRight, kCodeSpace, kCodeEnter, kCodeBackspace, kCodeInsert, kCodeDelete,
    kCodeHome, kCodekEnd, kCodePageUp, kCodePageDown,

    //!< 功能键
    kCodeF1, kCodeF2, kCodeF3, kCodeF4, kCodeF5, kCodeF6,
    kCodeF7, kCodeF8, kCodeF9, kCodeF10, kCodeF11, kCodeF12,

    //!< 字母键
    kCodeA, kCodeB, kCodeC, kCodeD, kCodeE, kCodeF, kCodeG,
    kCodeH, kCodeI, kCodeJ, kCodeK, kCodeL, kCodeM, kCodeN,
    kCodeO, kCodeP, kCodeQ, kCodeR, kCodeS, kCodeT, kCodeU,
    kCodeV, kCodeW, kCodeX, kCodeY, kCodeZ,

    //!< 数字键
    kCodeNum0, kCodeNum1, kCodeNum2, kCodeNum3, kCodeNum4,
    kCodeNum5, kCodeNum6, kCodeNum7, kCodeNum8, kCodeNum9,

    //!< 小键盘
    kCodeNumpad0, kCodeNumpad1, kCodeNumpad2, kCodeNumpad3, kCodeNumpad4,
    kCodeNumpad5, kCodeNumpad6, kCodeNumpad7, kCodeNumpad8, kCodeNumpad9,
    kCodeNumpadDivide, kCodeNumpadMultiply, kCodeNumpadMinus, kCodeNumpadPlus,
    kCodeNumpadEnter, kCodeNumpadPeriod,


    //!< 方向键
    kCodeUpArrow, kCodeDownArrow, kCodeLeftArrow, kCodeRightArrow,

    //!< 符号键
    kCodeMinus,     /*-*/ kCodeEquals,  /*=*/ kCodeLeftBracket, /*[*/kCodeRightBrackket,    /*]*/kCodeBackkslash,   /*\*/
    kCodeSemicolon, /*;*/kCodeQuote,    /*'*/kCodeComma,        /*,*/kCodePeriod,           /*.*/kCodeSlash,        /*/*/

    //!< 鼠标
    kCodeMouseLeft, kCodeMouseRight, kCodeMouseMiddle,
    kCodeMouseX1,   //!< 侧键1
    kCodeMouseX2,   //!< 侧键2
    kCodeMouseWheel,    //!< 中键
};

//!< 构造函数
iCAX::Services::CWinInputService::CWinInputService(IN std::shared_ptr<WinWindowService> pWindowService_)
    : m_InputSnapshot()
    , m_pWindowSercice(pWindowService_)
{
    //memset(&m_InputSnapshot, 0, sizeof(m_InputSnapshot));
}

//!< 析构函数
iCAX::Services::CWinInputService::~CWinInputService()
{
}

//!< 判断某个键是否按下
bool iCAX::Services::CWinInputService::GetKeyDown(IN const uintptr_t& Ptr_, IN const KeyCode& Key_) const
{
    if (Ptr_ == m_hwndCurrent && m_hwndCurrent != 0)
    {
        return m_InputSnapshot.mapCurrentKeyStates.at(Key_) && !m_InputSnapshot.mapPreviousKeyStates.at(Key_);
    }
    return false;
}

//!< 判断某个键是否处于按下的状态
bool iCAX::Services::CWinInputService::GetKey(IN const uintptr_t& Ptr_, IN const KeyCode& Key_) const
{
    if (Ptr_ == m_hwndCurrent && m_hwndCurrent != 0)
    {
        return m_InputSnapshot.mapCurrentKeyStates.at(Key_);
    }
    return false;
}

//!< 判断某个键是否处于抬起
bool iCAX::Services::CWinInputService::GetKeyUp(IN const uintptr_t& Ptr_, IN const KeyCode& Key_) const
{
    if (Ptr_ == m_hwndCurrent && m_hwndCurrent != 0)
    {
        return !m_InputSnapshot.mapCurrentKeyStates.at(Key_) && m_InputSnapshot.mapPreviousKeyStates.at(Key_);
    }
    return false;
}

//!< 获取鼠标位置
iCAX::Math::Point2 iCAX::Services::CWinInputService::GetMousePosition(IN const uintptr_t& Ptr_) const
{
    if (Ptr_ == m_hwndCurrent && m_hwndCurrent != 0)
    {
        return m_InputSnapshot.ptCurrentMousePosition;
    }
    return {};
}

//!< 获取鼠标偏移量
iCAX::Math::Vector2 iCAX::Services::CWinInputService::GetMouseDelta(IN const uintptr_t& Ptr_) const
{
    if (Ptr_ == m_hwndCurrent && m_hwndCurrent != 0)
    {
        return m_InputSnapshot.ptCurrentMousePosition - m_InputSnapshot.ptPreMousePosition;
    }
    return {};
}

//!< 获取鼠标滚轮偏移量
double iCAX::Services::CWinInputService::GetMouseWheelDelta(IN const uintptr_t& Ptr_) const
{
    if (Ptr_ == m_hwndCurrent && m_hwndCurrent != 0)
    {
        return m_InputSnapshot.nCurrentMouseWheel - m_InputSnapshot.nPreMouseWheel;
    }
    return 0;
}

//!< 获取触摸点数
int iCAX::Services::CWinInputService::GetTouchCount(IN const uintptr_t& Ptr_) const
{
    if (Ptr_ == m_hwndCurrent && m_hwndCurrent != 0)
    {
        return m_InputSnapshot.nTouchCount;
    }
    return 0;
}

//!< 获取指定触点
iCAX::Services::TouchPoint iCAX::Services::CWinInputService::GetTouch(IN const uintptr_t& Ptr_, IN const int& nIndex_) const
{
    if (Ptr_ == m_hwndCurrent && m_hwndCurrent != 0)
    {
        return m_InputSnapshot.lstTouchPoints.at(nIndex_);
    }
    return {};
}


//!< 每帧后触发
void iCAX::Services::CWinInputService::UpdateSnap()
{
    m_hwndPrevious = m_hwndCurrent;
    m_hwndCurrent = reinterpret_cast<uintptr_t>(m_pWindowSercice->GetActiveWindowHandle());
    if (m_hwndCurrent == 0)
    {
        return;
    }
    if (m_hwndCurrent != m_hwndPrevious)//!< 切换了窗口，一切重来
    {
        // 获取按键状态
        for (const auto& key : s_AllKeys)
        {
            m_InputSnapshot.mapCurrentKeyStates[key] = GetKeyState(key);
        }

        //!< 获取鼠标位置和偏移
        POINT p;
        GetCursorPos(&p);  //!< 获取鼠标当前位置
        ScreenToClient(reinterpret_cast<HWND>(m_hwndCurrent), &p);     //!< 转换到客户区坐标
        m_InputSnapshot.ptCurrentMousePosition = { static_cast<double>(p.x),static_cast<double>(p.y) };


        //!< 获取鼠标滚轮偏移
        m_InputSnapshot.nCurrentMouseWheel = GET_WHEEL_DELTA_WPARAM(GetMessageExtraInfo());

        m_InputSnapshot.mapPreviousKeyStates = m_InputSnapshot.mapCurrentKeyStates;
        m_InputSnapshot.ptPreMousePosition = m_InputSnapshot.ptCurrentMousePosition;
        m_InputSnapshot.nPreMouseWheel = m_InputSnapshot.nCurrentMouseWheel;
        m_InputSnapshot.nTouchCount = 0;
        m_InputSnapshot.lstTouchPoints = {};
    }
    else
    {
        m_InputSnapshot.mapPreviousKeyStates = m_InputSnapshot.mapCurrentKeyStates;
        m_InputSnapshot.ptPreMousePosition = m_InputSnapshot.ptCurrentMousePosition;
        m_InputSnapshot.nPreMouseWheel = m_InputSnapshot.nCurrentMouseWheel;
        m_InputSnapshot.nTouchCount = 0;
        m_InputSnapshot.lstTouchPoints = {};

        // 获取按键状态
        for (const auto& key : s_AllKeys)
        {
            m_InputSnapshot.mapCurrentKeyStates[key] = GetKeyState(key);
        }

        //!< 获取鼠标位置和偏移
        POINT p;
        GetCursorPos(&p);  //!< 获取鼠标当前位置
        ScreenToClient(reinterpret_cast<HWND>(m_hwndCurrent), &p);     //!< 转换到客户区坐标
        m_InputSnapshot.ptCurrentMousePosition = { static_cast<double>(p.x),static_cast<double>(p.y) };


        //!< 获取鼠标滚轮偏移
        m_InputSnapshot.nCurrentMouseWheel = GET_WHEEL_DELTA_WPARAM(GetMessageExtraInfo());
    }
}
