#pragma once
#include "PDO.h"
#include <unordered_map>
#include "IPDOSlot.h"

namespace iCAX
{
    namespace PDO
    {
        /*
        * @brief PDO对象集线器
        */
        class _PDO_EXP IPDOHub
        {
        public:
            /*
            * @brief 构造函数
            */
            IPDOHub() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IPDOHub() = default;

        public:
            /*
            * @brief 获取槽
            * @param [in] nPDOID_ PDO ID
            */
            virtual IPDOSlot& GetSlot(IN const PDOID& nPDOID_) = 0;

            /*
            * @brief 交换入槽
            */
            virtual void SwapInSlot() = 0;

            /*
            * @brief 交换出槽
            */
            virtual void SwapOutSlot() = 0;
        };

        /*
        * @brief 生成PDO集线器
        * @param [in] Descs_
        * @return std::shared_ptr<IPDOHub> 
        */
        _PDO_EXP std::shared_ptr<IPDOHub> GeneratePDOHub(IN std::vector<PDODecl> Descs_);
    }
}
