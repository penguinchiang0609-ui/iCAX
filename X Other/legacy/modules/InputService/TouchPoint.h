#pragma once
#include "InputService.h"
#include "../Math/Point2.h"

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 触摸点
        */
        struct _INPUTSERVICE_EXP TouchPoint
        {
            int id;                             //!< 唯一 ID（手指标识）
            iCAX::Math::Point2 ptPosition;      //!< 当前位置
            iCAX::Math::Vector2 vecDelta;       //!< 本帧移动
            enum class PhaseState 
            {
                kTouchBegan,        //!< 按下
                kTouchMoved,        //!< 移动
                kTouchStationary,   //!< 按下但没动
                kTouchEnded,        //!< 抬起
                kTouchCanceled      //!< 系统中断
            } Phase;
        }; 
    }
}