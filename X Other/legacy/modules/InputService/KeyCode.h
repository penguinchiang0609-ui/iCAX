#pragma once
#include "InputService.h"

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 键编码
        */
        enum _INPUTSERVICE_EXP KeyCode
        {
            kCodeNone = 0,

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

    }
}