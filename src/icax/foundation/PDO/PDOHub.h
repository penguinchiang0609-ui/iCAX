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
        * @brief PDO对象集线器
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
            */
            virtual void Intialize(IN std::vector<PDODecl> Descs_);

            /*
            * @brief 释放所有槽
            */
            virtual void Clear();

        public:
            /*
            * @brief 获取槽
            * @param [in] nPDOID_ PDO ID
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
