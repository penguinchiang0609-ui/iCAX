#pragma once
#include "InputService.h"
#include <unordered_map>
#include <string>
#include <array>
#include "../Math/Point2.h"
#include "KeyCode.h"
#include "TouchPoint.h"

namespace iCAX
{
    namespace Services
    {
        // 输入数据快照结构体
        struct _INPUTSERVICE_EXP InputSnapshot
        {
            std::unordered_map<KeyCode, bool> mapCurrentKeyStates = {};      //!< 当前帧按键状态
            std::unordered_map<KeyCode, bool> mapPreviousKeyStates = {};     //!< 上一帧按键状态
            iCAX::Math::Point2 ptPreMousePosition = {};                      //!< 上一帧鼠标位置
            iCAX::Math::Point2 ptCurrentMousePosition = {};                  //!< 当前帧鼠标位置
            double nCurrentMouseWheel = 0;                                  //!< 当前帧鼠标滚轮量
            double nPreMouseWheel = 0;                                      //!< 上一帧鼠标滚轮量
            int nTouchCount = 0;                                            //!< 触摸点数量
            std::vector<TouchPoint> lstTouchPoints = {};                     //!< 触摸点信息
        };
    }
}