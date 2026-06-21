#pragma once
#include "PDO.h"
#include "PDODecl.h"

namespace iCAX
{
    namespace PDO
    {
        /*
        * @brief 过程状态数据槽，双缓冲设计
        * @remark
        *   1、双缓冲设计，读写互不干扰
        *   2、写入完成后，调用 MarkWriteReady 标记写入完成
        *   3、调用 SwapBuffersIfReady 交换读写缓冲区
        *   4、读取数据时，调用 GetReadData 获取数据指针
        *   5、写入数据时，调用 GetWriteData 获取数据指针
        *   6、线程安全设计
        *   7、用于高频可丢弃数据传输
        */
        class _PDO_EXP IPDOSlot
        {
        public:
            /*
            * @brief 构造函数
            */
            IPDOSlot() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IPDOSlot() = default;

        public:
            /*
            * @brief 获取头部信息
            * @return 槽声明信息引用，生命周期与 Slot 相同。
            */
            virtual const PDODecl& GetHeader() const = 0;

            /*
            * @brief 获取写数据
            * @return 当前写缓冲指针，大小为 GetHeader().nPayloadSize。
            * @details
            *   调用方写入完成后必须调用 MarkWriteReady，否则读缓冲不会交换。
            *   指针只在下一次 SwapBuffersIfReady 前稳定，不应长期保存。
            */
            virtual void* GetWriteData() = 0;

            /*
            * @brief 标记写结束
            * @details
            *   标记当前写缓冲已经准备好。实际读写缓冲交换由 Hub 在帧边界调用。
            */
            virtual void MarkWriteReady() = 0;

            /*
            * @brief 获取读数据
            * @return 当前读缓冲指针，大小为 GetHeader().nPayloadSize。
            * @details
            *   返回的是最近一次成功交换后的稳定快照。
            *   如果写侧持续写入但未到交换点，读侧仍看到旧数据。
            */
            virtual const void* GetReadData() const = 0;

            /*
            * @brief 双缓冲交换
            * @details
            *   只有写缓冲被 MarkWriteReady 标记后才会交换。
            *   交换后旧读缓冲成为新的写缓冲，并被标记为空。
            */
            virtual void SwapBuffersIfReady() = 0;
        };
    }
}
