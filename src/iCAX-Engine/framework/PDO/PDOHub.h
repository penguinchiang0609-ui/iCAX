#pragma once
#include "PDO.h"
#include <unordered_map>
#include "IPDOHub.h"
#include "SharedPDOArena.h"

namespace iCAX
{
    namespace PDO
    {
        /*
        * @brief PDO 对象集线器实现。
        * @details
        *   CPDOHub 按 PDOID 保存 Slot。Initialize 会根据声明创建共享内存 Arena，
        *   所有 Slot 均由 Windows OS shared memory 承载。
        */
        class _PDO_EXP CPDOHub final : public IPDOHub
        {
        public:
            /*
            * @brief 构造函数
            */
            CPDOHub();

            /*
            * @brief 析构函数
            */
            virtual ~CPDOHub();

        public:
            /*
            * @brief 初始化槽
            * @param [in] Descs_ 槽描述列表
            * @details
            *   每个声明会在共享内存 Arena 中创建一个双缓冲 Slot。
            *   重复 PDOID 或非法声明会抛出异常。
            */
            virtual void Intialize(IN std::vector<PDODecl> Descs_);

            /*
            * @brief 释放所有槽
            * @details 清空后，旧 Slot 引用不再可用，上层不应继续保存。
            */
            virtual void Clear();

        public:
            /*
            * @brief 获取槽
            * @param [in] nPDOID_ PDO ID
            * @return 指定 ID 对应的 Slot。
            * @throws std::runtime_error 指定 ID 不存在。
            */
            virtual IPDOSlot& GetSlot(IN const PDOID& nPDOID_) override;

            /*
            * @brief 获取当前 Hub 使用的共享内存名称。
            * @return Windows file mapping 名称；Hub 未初始化时返回空字符串。
            */
            virtual const std::wstring& GetSharedArenaName() const noexcept override;

            /*
            * @brief 获取当前 Hub 使用的共享内存大小。
            * @return Arena 字节数；Hub 未初始化时返回 0。
            */
            virtual uint64_t GetSharedArenaSize() const noexcept override;

            /*
            * @brief 获取当前 Hub 的 PDO 声明快照。
            * @return PDO 声明列表；Hub 未初始化时返回空列表。
            */
            virtual std::vector<PDODecl> GetPDODeclarations() const override;

            /*
            * @brief 获取当前 Hub 的共享内存 Arena。
            * @return Arena 指针；Hub 未初始化时返回 nullptr。
            */
            std::shared_ptr<CSharedPDOArena> GetSharedArena() const noexcept;

            /*
            * @brief 交换入槽
            */
            virtual void SwapInSlot() override;

            /*
            * @brief 交换出槽
            */
            virtual void SwapOutSlot() override;

        private:
            std::wstring m_strArenaName;
            std::shared_ptr<CSharedPDOArena> m_pArena;
            std::unordered_map<uint64_t, std::shared_ptr<CSharedPDOSlot>> m_mapSlots;
        };
    }
}
