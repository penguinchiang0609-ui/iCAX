#pragma once
#include "PDO.h"
#include <unordered_map>
#include "IPDOHub.h"
#include "PDOSlot.h"

namespace iCAX
{
    namespace PDO
    {
        /*
        * @brief PDO 对象集线器实现。
        * @details
        *   CPDOHub 按 PDOID 保存 Slot。Initialize 会根据声明创建固定大小的双缓冲槽。
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
            *   每个声明会创建一个 CPDOSlot。重复 PDOID 会以后一次插入结果为准，
            *   因此上层应保证声明唯一。
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
            * @brief 交换入槽
            */
            virtual void SwapInSlot() override;

            /*
            * @brief 交换出槽
            */
            virtual void SwapOutSlot() override;

        private:
            std::unordered_map<uint64_t, std::shared_ptr<CPDOSlot>> m_mapSlots;
        };
    }
}
