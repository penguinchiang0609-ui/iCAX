#pragma once
#include "InputService.h"
#include "KeyCode.h"
#include "../Math/Point2.h"
#include "TouchPoint.h"
#include <cstdint>

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 输入服务
        */
        class _INPUTSERVICE_EXP IInputService
        {
        public:
            /*
            * @brief 构造函数
            */
            IInputService() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IInputService() = default;

        public:
            /*
            * @brief 判断某个键是否按下
            * @param [in] Ptr_ 句柄
            * @param [in] Key_
            * @return bool
            */
            virtual bool GetKeyDown(IN const uintptr_t& Ptr_, IN const KeyCode& Key_) const = 0;

            /*
            * @brief 判断某个键是否处于按下的状态
            * @param [in] Ptr_ 句柄
            * @param [in] Key_
            * @return bool
            */
            virtual bool GetKey(IN const uintptr_t& ptr_, IN const KeyCode& Key_) const = 0;

            /*
            * @brief 判断某个键是否处于抬起
            * @param [in] Ptr_ 句柄
            * @param [in] Key_
            * @return bool
            */
            virtual bool GetKeyUp(IN const uintptr_t& ptr_, IN const KeyCode& Key_) const = 0;

            /*
            * @brief 获取鼠标位置
            * @param [in] Ptr_ 句柄
            * @return iCAX::Data::Point2
            */
            virtual iCAX::Math::Point2 GetMousePosition(IN const uintptr_t& ptr_) const = 0;

            /*
            * @brief 获取鼠标偏移量
            * @param [in] Ptr_ 句柄
            * @return iCAX::Data::Vector2
            */
            virtual iCAX::Math::Vector2 GetMouseDelta(IN const uintptr_t& ptr_) const = 0;

            /*
            * @brief 获取鼠标滚轮偏移量
            * @param [in] Ptr_ 句柄
            * @return double
            */
            virtual double GetMouseWheelDelta(IN const uintptr_t& ptr_) const = 0;

            /*
            * @brief 获取触摸点数
            * @param [in] Ptr_ 句柄
            * @return int
            */
            virtual int GetTouchCount(IN const uintptr_t& ptr_) const = 0;

            /*
            * @brief 获取指定触点
            * @param [in] Ptr_ 句柄
            * @param [in] nIndex_
            * @return TouchPoint
            */
            virtual TouchPoint GetTouch(IN const uintptr_t& ptr_, IN const int& nIndex_) const = 0;

            /*
            * @brief 更新快照
            * @remark 每一帧的最后更新快照，下一帧使用。由System层 InputBehaviour负责调用
            */
            virtual void UpdateSnap() = 0;
        };
    }
}