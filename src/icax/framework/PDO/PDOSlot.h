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
            * @param [in] Header_ PDO 槽声明，包含 ID、方向、版本和负载大小。
            * @throws std::invalid_argument 当负载大小不合法时抛出。
            */
            CPDOSlot(IN const PDODecl& Header_);

            /*
            * @brief 析构函数
            */
            virtual ~CPDOSlot();

        public:
            /*
            * @brief 获取头部信息
            * @return 构造时传入的 PDO 声明。
            */
            virtual const PDODecl& GetHeader() const override;

            /*
            * @brief 获取写数据
            * @return 当前写缓冲指针。
            * @details
            *   写入线程应先填充该指针指向的固定大小内存，再调用 MarkWriteReady。
            *   多次写入未交换时，后一次写入会覆盖同一写缓冲。
            */
            virtual void* GetWriteData() override;

            /*
            * @brief 标记写结束
            * @details 使用 release 语义发布写缓冲，等待帧边界交换。
            */
            virtual void MarkWriteReady() override;

            /*
            * @brief 获取读数据
            * @return 当前读缓冲指针。
            * @details 读侧看到的是最近一次成功交换后的完整快照。
            */
            virtual const void* GetReadData() const override;

            /*
            * @brief 双缓冲交换
            * @details
            *   如果当前写缓冲 Ready，则交换读写索引；否则保持旧读缓冲不变。
            *   交换完成后，旧读缓冲成为新写缓冲并标记为空。
            */
            virtual void SwapBuffersIfReady() override;

        private:
            PDODecl m_Decl;                         //!< 声明信息。
            uint8_t* m_pDataArray[2];                //!< 两块固定大小缓冲区。
            enum class ESlotStatus : uint8_t { Empty = 0, Ready };
            std::atomic<ESlotStatus> m_nSlotStatus[2]; //!< 每块缓冲区是否已有完整写入。
            std::atomic<uint8_t> m_nWriteIndex;        //!< 当前写缓冲下标。
            std::atomic<uint8_t> m_nReadIndex;         //!< 当前读缓冲下标。
        };
    }
}
