#pragma once

#include "IPDOSlot.h"

#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace PDO
    {
        inline constexpr uint32_t kSharedPDOArenaMagic = 0x4F445049u; // "IPDO", little endian.
        inline constexpr uint32_t kSharedPDOArenaVersion = 2;
        inline constexpr uint32_t kSharedPDOBufferCount = 2;
        inline constexpr int32_t kSharedPDOReadyNone = -1;
        inline constexpr long kSharedPDOBufferFree = 0;
        inline constexpr long kSharedPDOBufferPublished = 1;
        inline constexpr long kSharedPDOBufferWriting = 2;
        inline constexpr long kSharedPDOBufferReady = 3;
        inline constexpr long kSharedPDOBufferReading = 4;

        /*
        * @brief 共享内存 PDO Arena 头。
        * @details
        *   该结构只描述共享内存块的布局，不描述 PDO 字段。
        *   PDO 的字段布局由产品 C++ 结构和前端 TS wrapper 共同约定。
        */
        struct _PDO_EXP SharedPDOArenaHeader final
        {
            uint32_t nMagic = kSharedPDOArenaMagic;
            uint32_t nVersion = kSharedPDOArenaVersion;
            uint32_t nSlotCount = 0;
            uint32_t nHeaderSize = sizeof(SharedPDOArenaHeader);
            uint64_t nArenaSize = 0;
            uint64_t nSlotTableOffset = 0;
            uint64_t nPayloadOffset = 0;
        };

        /*
        * @brief 共享内存 PDO 槽头。
        * @details
        *   槽头保存固定 PDO 的 ID、payload 协议版本、方向、载荷大小、双缓冲状态和运行期数据版本。
        *   不保存字段级描述；前后端必须提前约定 Payload layout。
        *   PDO 允许丢帧；读侧通过 reader count 声明读租约，写侧复用前等待租约释放。
        */
        struct _PDO_EXP SharedPDOSlotHeader final
        {
            PDOID nID = 0;
            uint32_t nVersion = 0;
            uint32_t nDirection = kDirectionNil;
            uint32_t nPayloadSize = 0;
            uint32_t nReserved = 0;
            uint64_t nBufferOffset[kSharedPDOBufferCount]{};
            volatile long nBufferState[kSharedPDOBufferCount]{};
            volatile long nReaderCount[kSharedPDOBufferCount]{};
            volatile long nPublishedIndex = 0;
            volatile long nWriteIndex = 1;
            volatile long nReadyIndex = kSharedPDOReadyNone;
            volatile long nSequence = 0;
            volatile unsigned long long nBufferDataVersion[kSharedPDOBufferCount]{};
            volatile unsigned long long nPublishedDataVersion = 0;
            volatile unsigned long long nLatestDataVersion = 0;
        };

        /*
        * @brief 共享内存 PDO 槽视图。
        * @details
        *   该对象不拥有内存，只引用 CSharedPDOArena 映射出的共享内存。
        *   写侧按 BeginWrite/TryBeginWrite -> MarkWriteReady/CancelWrite -> SwapBuffersIfReady 使用。
        */
        class _PDO_EXP CSharedPDOSlot final : public IPDOSlot
        {
        public:
            CSharedPDOSlot() = default;
            CSharedPDOSlot(IN uint8_t* pArenaBase_, IN SharedPDOSlotHeader* pHeader_);
            ~CSharedPDOSlot() override = default;

            CSharedPDOSlot(IN const CSharedPDOSlot&) = default;
            CSharedPDOSlot& operator=(IN const CSharedPDOSlot&) = default;

        public:
            bool IsValid() const noexcept;
            uint32_t GetSequence() const noexcept;
            uint32_t GetPublishedIndex() const noexcept;
            uint32_t GetBufferState(IN uint32_t nIndex_) const;
            uint32_t GetReaderCount(IN uint32_t nIndex_) const;
            uint64_t GetBufferDataVersion(IN uint32_t nIndex_) const;
            bool IsReadSnapshotValid(IN uint32_t nSequence_, IN uint32_t nPublishedIndex_) const noexcept;
            uint64_t GetReadBufferOffset() const;
            uint64_t GetWriteBufferOffset() const;
            const SharedPDOSlotHeader& GetSharedHeader() const;

        public:
            const PDODecl& GetHeader() const override;
            void* BeginWrite() override;
            bool TryBeginWrite(OUT void*& pWriteData_) override;
            bool TryBeginWriteIfNewer(IN uint64_t nDataVersion_, OUT void*& pWriteData_) override;
            void MarkWriteReady(IN uint64_t nDataVersion_) override;
            void CancelWrite() override;
            const void* BeginRead(
                OUT uint32_t& nSequence_,
                OUT uint32_t& nPublishedIndex_,
                OUT uint64_t& nDataVersion_) override;
            void EndRead(IN uint32_t nPublishedIndex_) override;
            uint64_t GetPublishedDataVersion() const noexcept override;
            uint64_t GetLatestDataVersion() const noexcept override;
            void SwapBuffersIfReady() override;

        private:
            uint8_t* GetBufferAddress(IN uint32_t nIndex_) const;
            bool TryClaimWriteBufferOnce(IN uint32_t nIndex_) const;
            void RequireValid() const;

        private:
            uint8_t* m_pArenaBase = nullptr;
            SharedPDOSlotHeader* m_pHeader = nullptr;
            PDODecl m_Decl{};
        };

        /*
        * @brief Windows OS shared memory backed PDO Arena。
        * @details
        *   Browser/backend 进程创建 Arena，CEF renderer 进程按名称打开同一 Arena。
        *   Renderer 可把 GetBaseAddress()/GetArenaSize() 包装成 V8 ArrayBuffer。
        */
        class _PDO_EXP CSharedPDOArena final
        {
        public:
            static std::shared_ptr<CSharedPDOArena> Create(
                IN std::wstring strName_,
                IN const std::vector<PDODecl>& Decls_);

            static std::shared_ptr<CSharedPDOArena> Open(IN std::wstring strName_);

        public:
            ~CSharedPDOArena();

            CSharedPDOArena(IN const CSharedPDOArena&) = delete;
            CSharedPDOArena& operator=(IN const CSharedPDOArena&) = delete;

        public:
            const std::wstring& GetName() const noexcept;
            void* GetBaseAddress() const noexcept;
            uint64_t GetArenaSize() const noexcept;
            const SharedPDOArenaHeader& GetHeader() const;
            std::vector<PDODecl> GetDeclarations() const;
            CSharedPDOSlot GetSlot(IN PDOID nID_) const;

        private:
            CSharedPDOArena() = default;

            void InitializeCreatedArena(IN const std::vector<PDODecl>& Decls_);
            void ValidateOpenedArena() const;
            SharedPDOArenaHeader* GetMutableHeader() const;
            SharedPDOSlotHeader* GetSlotTable() const;

        private:
            std::wstring m_strName;
            void* m_hMapping = nullptr;
            uint8_t* m_pBase = nullptr;
            uint64_t m_nArenaSize = 0;
        };
    }
}
