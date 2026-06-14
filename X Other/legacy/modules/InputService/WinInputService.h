#pragma once
#include "InputService.h"
#include "IInputService.h"
#include "InputSnapshot.h"
#include "../WindowService/WinWindowService.h"

namespace iCAX
{
    namespace Services
    {
        class _INPUTSERVICE_EXP CWinInputService : public IInputService
        {
        public:
            /*
            * @brief 构造函数
            */
            CWinInputService(IN std::shared_ptr<WinWindowService> pWindowService_);

            /*
            * @brief 析构函数
            */
            virtual ~CWinInputService();

        public:
            /*
            * @brief 判断某个键是否按下
            * @param [in] Ptr_ 句柄
            * @param [in] Key_
            * @return bool
            */
            virtual bool GetKeyDown(IN const uintptr_t& Ptr_, IN const KeyCode& Key_) const override;

            /*
            * @brief 判断某个键是否处于按下的状态
            * @param [in] Ptr_ 句柄
            * @param [in] Key_
            * @return bool
            */
            virtual bool GetKey(IN const uintptr_t& ptr_, IN const KeyCode& Key_) const override;

            /*
            * @brief 判断某个键是否处于抬起
            * @param [in] Ptr_ 句柄
            * @param [in] Key_
            * @return bool
            */
            virtual bool GetKeyUp(IN const uintptr_t& ptr_, IN const KeyCode& Key_) const override;

            /*
            * @brief 获取鼠标位置
            * @param [in] Ptr_ 句柄
            * @return iCAX::Data::Point2
            */
            virtual iCAX::Math::Point2 GetMousePosition(IN const uintptr_t& ptr_) const override;

            /*
            * @brief 获取鼠标偏移量
            * @param [in] Ptr_ 句柄
            * @return iCAX::Data::Vector2
            */
            virtual iCAX::Math::Vector2 GetMouseDelta(IN const uintptr_t& ptr_) const override;

            /*
            * @brief 获取鼠标滚轮偏移量
            * @param [in] Ptr_ 句柄
            * @return double
            */
            virtual double GetMouseWheelDelta(IN const uintptr_t& ptr_) const override;

            /*
            * @brief 获取触摸点数
            * @param [in] Ptr_ 句柄
            * @return int
            */
            virtual int GetTouchCount(IN const uintptr_t& ptr_) const override;

            /*
            * @brief 获取指定触点
            * @param [in] Ptr_ 句柄
            * @param [in] nIndex_
            * @return TouchPoint
            */
            virtual TouchPoint GetTouch(IN const uintptr_t& ptr_, IN const int& nIndex_) const override;

            /*
            * @brief 更新快照
            * @remark 每一帧的最后更新快照，下一帧使用。由System层 InputBehaviour负责调用
            */
            virtual void UpdateSnap() override;

        private:
            uintptr_t m_hwndPrevious;                                 //!< 前一帧窗口句柄
            uintptr_t m_hwndCurrent;                                  //!< 当前帧窗口
            InputSnapshot m_InputSnapshot;                          //!< 当前帧的输入快照
            std::shared_ptr<WinWindowService> m_pWindowSercice;     //!< 
        };
    }
}