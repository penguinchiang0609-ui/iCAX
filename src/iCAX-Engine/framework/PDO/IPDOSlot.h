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
        *   2、写入完成后，调用 MarkWriteReady(dataVersion) 标记写入完成
        *   3、调用 SwapBuffersIfReady 交换读写缓冲区
        *   4、读取数据时，优先使用 CPDOReadLease 或 BeginRead/EndRead
        *   5、写入数据时，优先使用 CPDOWriteLease 或 BeginWrite/MarkWriteReady/CancelWrite
        *   6、缓冲状态使用原子操作维护
        *   7、用于高频可丢弃数据传输；读侧通过 BeginRead/EndRead 声明读租约
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
            * @brief 开始写入当前写缓冲。
            * @return 当前写缓冲指针，大小为 GetHeader().nPayloadSize。
            * @details
            *   调用方写入完成后必须调用 MarkWriteReady，否则读缓冲不会交换。
            *   调用方决定放弃本次写入时必须调用 CancelWrite。
            *   指针只在下一次 SwapBuffersIfReady 前稳定，不应长期保存。
            */
            virtual void* BeginWrite() = 0;

            /*
            * @brief 尝试开始写入当前写缓冲。
            * @param [out] pWriteData_ 成功时返回写缓冲指针；失败时返回 nullptr。
            * @return true 表示已经取得写缓冲；false 表示写缓冲当前不可用，本帧可丢弃。
            * @details
            *   高频 PDO 推荐优先使用非阻塞写入，避免读侧短暂持有租约时卡住帧循环。
            */
            virtual bool TryBeginWrite(OUT void*& pWriteData_) = 0;

            /*
            * @brief 当源数据版本比 Slot 内最新版本更新时才尝试开始写入。
            * @param [in] nDataVersion_ 源数据当前版本；0 表示无有效数据版本。
            * @param [out] pWriteData_ 返回当前写缓冲指针；无需写入时返回 nullptr。
            * @return true 表示调用方应该序列化并在完成后调用 MarkWriteReady(nDataVersion_)；
            *   false 表示 Slot 内已经有不低于该版本的数据，或写缓冲当前不可用，本帧可跳过。
            * @details
            *   该接口用于 Mesh、实例矩阵等重序列化成本较高的数据。
            *   它会同时比较 published buffer 和 ready buffer 的数据版本，避免同一版本在交换前重复序列化。
            */
            virtual bool TryBeginWriteIfNewer(IN uint64_t nDataVersion_, OUT void*& pWriteData_) = 0;

            /*
            * @brief 标记写结束
            * @param [in] nDataVersion_ 本次写入对应的源数据版本。
            * @details
            *   标记当前写缓冲已经准备好。实际读写缓冲交换由 Hub 在帧边界调用。
            *   nDataVersion_ 必须大于 Slot 内已经发布或等待发布的数据版本。
            */
            virtual void MarkWriteReady(IN uint64_t nDataVersion_) = 0;

            /*
            * @brief 取消当前写入。
            * @details
            *   调用方已经取得写缓冲但决定不发布时，应调用该接口。
            *   CPDOWriteLease 在未 Commit 的析构路径会自动调用 CancelWrite。
            */
            virtual void CancelWrite() = 0;

            /*
            * @brief 开始读取当前 published buffer。
            * @param [out] nSequence_ 返回读取开始时的 sequence。
            * @param [out] nPublishedIndex_ 返回被锁定读取的 buffer index。
            * @param [out] nDataVersion_ 返回该 published buffer 对应的源数据版本。
            * @return 当前可读缓冲指针，大小为 GetHeader().nPayloadSize。
            * @details
            *   BeginRead 会登记读租约，写侧在复用该 buffer 前会等待租约释放。
            *   调用方必须在读取完成后调用 EndRead(nPublishedIndex_)。
            */
            virtual const void* BeginRead(
                OUT uint32_t& nSequence_,
                OUT uint32_t& nPublishedIndex_,
                OUT uint64_t& nDataVersion_) = 0;

            /*
            * @brief 结束读取。
            * @param [in] nPublishedIndex_ BeginRead 返回的 buffer index。
            */
            virtual void EndRead(IN uint32_t nPublishedIndex_) = 0;

            /*
            * @brief 获取当前 published buffer 的源数据版本。
            * @return 当前读侧可见数据版本；0 表示尚未发布有效数据。
            */
            virtual uint64_t GetPublishedDataVersion() const noexcept = 0;

            /*
            * @brief 获取 Slot 内最新的数据版本。
            * @return published buffer 与 ready buffer 中较新的数据版本。
            * @details
            *   写侧判断是否需要重序列化时应使用该接口，而不是只看 published 版本。
            */
            virtual uint64_t GetLatestDataVersion() const noexcept = 0;

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
