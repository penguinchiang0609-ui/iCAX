#pragma once
#include "PDO.h"
#include "PDODecl.h"
#include "IPDOSlot.h"

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
        class _PDO_EXP CPDOSlot final : public IPDOSlot
        {
        public:
            /*
            * @brief 构造函数
            */
            CPDOSlot(IN const PDODecl& Header_);

            /*
            * @brief 析构函数
            */
            virtual ~CPDOSlot();

        public:
            /*
            * @brief 获取头部信息
            */
            virtual const PDODecl& GetHeader() const override;

            /*
            * @brief 获取写数据
            */
            virtual void* GetWriteData() override;

            /*
            * @brief 标记写结束
            */
            virtual void MarkWriteReady() override;

            /*
            * @brief 获取读数据
            * @return const void*
            */
            virtual const void* GetReadData() const override;

            /*
            * @brief 双缓冲交换
            */
            virtual void SwapBuffersIfReady() override;

        private:
            PDODecl m_Decl;         //! 声明信息
            uint8_t* m_pDataArray[2];         //! 双缓冲数据
            enum class ESlotStatus : uint8_t { Empty = 0, Ready };
            std::atomic<ESlotStatus> m_nSlotStatus[2];  //! buffer 状态
            std::atomic<uint8_t> m_nWriteIndex;         //! 当前写 buffer
            std::atomic<uint8_t> m_nReadIndex;          //! 当前读 buffer
        };
    }
}
