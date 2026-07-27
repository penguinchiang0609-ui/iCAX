#pragma once

#include "IPDOSlot.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

namespace iCAX
{
    namespace PDO
    {
        /*
        * @brief PDO 读租约。
        * @details
        *   构造时调用 BeginRead，析构时自动 EndRead。
        *   产品代码应优先使用该类型读取 payload，避免遗漏 EndRead 导致写侧等待。
        */
        class CPDOReadLease final
        {
        public:
            explicit CPDOReadLease(IN IPDOSlot& Slot_)
                : m_pSlot(&Slot_)
            {
                m_pData = m_pSlot->BeginRead(m_nSequence, m_nBufferIndex, m_nDataVersion);
                try
                {
                    m_nPayloadSize = m_pSlot->GetBufferPayloadSize(m_nBufferIndex);
                }
                catch (...)
                {
                    m_pSlot->EndRead(m_nBufferIndex);
                    m_pSlot = nullptr;
                    m_pData = nullptr;
                    throw;
                }
            }

            ~CPDOReadLease()
            {
                Release();
            }

            CPDOReadLease(IN const CPDOReadLease&) = delete;
            CPDOReadLease& operator=(IN const CPDOReadLease&) = delete;

            CPDOReadLease(IN CPDOReadLease&& Other_) noexcept
            {
                MoveFrom(std::move(Other_));
            }

            CPDOReadLease& operator=(IN CPDOReadLease&& Other_) noexcept
            {
                if (this != &Other_)
                {
                    Release();
                    MoveFrom(std::move(Other_));
                }
                return *this;
            }

        public:
            const void* Data() const noexcept
            {
                return m_pData;
            }

            template<typename T>
            const T& As() const
            {
                return *static_cast<const T*>(m_pData);
            }

            uint32_t Sequence() const noexcept
            {
                return m_nSequence;
            }

            uint32_t BufferIndex() const noexcept
            {
                return m_nBufferIndex;
            }

            uint64_t DataVersion() const noexcept
            {
                return m_nDataVersion;
            }

            bool IsActive() const noexcept
            {
                return m_pSlot != nullptr;
            }

            uint64_t PayloadCapacity() const noexcept
            {
                return m_pSlot == nullptr
                    ? 0
                    : static_cast<uint64_t>(
                        m_pSlot->GetHeader().nPayloadSize);
            }

            uint64_t PayloadSize() const noexcept
            {
                return m_nPayloadSize;
            }

            void Release()
            {
                if (m_pSlot)
                {
                    m_pSlot->EndRead(m_nBufferIndex);
                    m_pSlot = nullptr;
                    m_pData = nullptr;
                    m_nSequence = 0;
                    m_nBufferIndex = 0;
                    m_nDataVersion = 0;
                    m_nPayloadSize = 0;
                }
            }

        private:
            void MoveFrom(IN CPDOReadLease&& Other_) noexcept
            {
                m_pSlot = Other_.m_pSlot;
                m_pData = Other_.m_pData;
                m_nSequence = Other_.m_nSequence;
                m_nBufferIndex = Other_.m_nBufferIndex;
                m_nDataVersion = Other_.m_nDataVersion;
                m_nPayloadSize = Other_.m_nPayloadSize;

                Other_.m_pSlot = nullptr;
                Other_.m_pData = nullptr;
                Other_.m_nSequence = 0;
                Other_.m_nBufferIndex = 0;
                Other_.m_nDataVersion = 0;
                Other_.m_nPayloadSize = 0;
            }

        private:
            IPDOSlot* m_pSlot = nullptr;
            const void* m_pData = nullptr;
            uint32_t m_nSequence = 0;
            uint32_t m_nBufferIndex = 0;
            uint64_t m_nDataVersion = 0;
            uint64_t m_nPayloadSize = 0;
        };

        /*
        * @brief PDO 写租约。
        * @details
        *   构造后持有当前写缓冲。调用 Commit 后写入会发布为 ready buffer；
        *   如果对象析构前没有 Commit，会自动 CancelWrite，避免 Slot 长期停留在 writing 状态。
        */
        class CPDOWriteLease final
        {
        public:
            CPDOWriteLease(IN IPDOSlot& Slot_, IN uint64_t nDataVersion_)
                : m_pSlot(&Slot_)
                , m_nDataVersion(nDataVersion_)
            {
                if (nDataVersion_ == 0)
                {
                    Reset();
                    throw std::invalid_argument("PDO write lease data version cannot be zero");
                }
                m_pData = Slot_.BeginWrite();
                m_nPayloadSize = PayloadCapacity();
            }

            ~CPDOWriteLease()
            {
                Cancel();
            }

            CPDOWriteLease(IN const CPDOWriteLease&) = delete;
            CPDOWriteLease& operator=(IN const CPDOWriteLease&) = delete;

            CPDOWriteLease(IN CPDOWriteLease&& Other_) noexcept
            {
                MoveFrom(std::move(Other_));
            }

            CPDOWriteLease& operator=(IN CPDOWriteLease&& Other_) noexcept
            {
                if (this != &Other_)
                {
                    Cancel();
                    MoveFrom(std::move(Other_));
                }
                return *this;
            }

        public:
            static std::optional<CPDOWriteLease> TryBeginIfNewer(
                IN IPDOSlot& Slot_,
                IN uint64_t nDataVersion_)
            {
                void* _pData = nullptr;
                if (!Slot_.TryBeginWriteIfNewer(nDataVersion_, _pData))
                {
                    return std::nullopt;
                }
                CPDOWriteLease _Lease(PrivateTag{}, Slot_, nDataVersion_, _pData);
                return std::optional<CPDOWriteLease>(std::move(_Lease));
            }

            void* Data() const noexcept
            {
                return m_pData;
            }

            template<typename T>
            T& As() const
            {
                return *static_cast<T*>(m_pData);
            }

            uint64_t DataVersion() const noexcept
            {
                return m_nDataVersion;
            }

            bool IsActive() const noexcept
            {
                return m_pSlot != nullptr;
            }

            uint64_t PayloadCapacity() const noexcept
            {
                return m_pSlot == nullptr
                    ? 0
                    : static_cast<uint64_t>(
                        m_pSlot->GetHeader().nPayloadSize);
            }

            uint64_t PayloadSize() const noexcept
            {
                return m_nPayloadSize;
            }

            void SetPayloadSize(IN uint64_t nPayloadSize_)
            {
                if (!m_pSlot)
                {
                    throw std::logic_error("PDO write lease is not active");
                }
                if (nPayloadSize_ == 0 || nPayloadSize_ > PayloadCapacity())
                {
                    throw std::out_of_range(
                        "PDO payload size is outside the slot capacity");
                }
                m_nPayloadSize = nPayloadSize_;
            }

            void Commit()
            {
                if (m_pSlot)
                {
                    m_pSlot->MarkWriteReady(m_nDataVersion, m_nPayloadSize);
                    Reset();
                }
            }

            void Commit(IN uint64_t nPayloadSize_)
            {
                SetPayloadSize(nPayloadSize_);
                Commit();
            }

            void Cancel()
            {
                if (m_pSlot)
                {
                    m_pSlot->CancelWrite();
                    Reset();
                }
            }

        private:
            struct PrivateTag
            {
            };

            CPDOWriteLease(
                IN PrivateTag,
                IN IPDOSlot& Slot_,
                IN uint64_t nDataVersion_,
                IN void* pData_)
                : m_pSlot(&Slot_)
                , m_nDataVersion(nDataVersion_)
                , m_pData(pData_)
            {
                m_nPayloadSize = PayloadCapacity();
            }

            void Reset() noexcept
            {
                m_pSlot = nullptr;
                m_pData = nullptr;
                m_nDataVersion = 0;
                m_nPayloadSize = 0;
            }

            void MoveFrom(IN CPDOWriteLease&& Other_) noexcept
            {
                m_pSlot = Other_.m_pSlot;
                m_pData = Other_.m_pData;
                m_nDataVersion = Other_.m_nDataVersion;
                m_nPayloadSize = Other_.m_nPayloadSize;
                Other_.Reset();
            }

        private:
            IPDOSlot* m_pSlot = nullptr;
            uint64_t m_nDataVersion = 0;
            uint64_t m_nPayloadSize = 0;
            void* m_pData = nullptr;
        };
    }
}
