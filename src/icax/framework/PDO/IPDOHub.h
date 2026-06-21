#pragma once
#include "PDO.h"
#include <unordered_map>
#include "IPDOSlot.h"

namespace iCAX
{
    namespace PDO
    {
        /*
        * @brief PDO 对象集线器。
        * @details
        *   Hub 持有一组 PDO Slot，并在帧边界按方向批量交换。
        *   它不解释负载内容，负载布局由注册 PDO 的业务双方约定。
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
            * @return 对应 PDO 槽。
            * @throws std::runtime_error 指定 ID 不存在。
            */
            virtual IPDOSlot& GetSlot(IN const PDOID& nPDOID_) = 0;

            /*
            * @brief 交换入槽
            * @details 交换方向包含 kDirection2Inner 的槽，通常在每帧开始调用。
            */
            virtual void SwapInSlot() = 0;

            /*
            * @brief 交换出槽
            * @details 交换方向包含 kDirection2External 的槽，通常在每帧结束调用。
            */
            virtual void SwapOutSlot() = 0;
        };

        /*
        * @brief 生成 PDO 集线器。
        * @param [in] Descs_ PDO 槽声明列表。
        * @return 已按声明创建好 Slot 的 Hub。
        */
        _PDO_EXP std::shared_ptr<IPDOHub> GeneratePDOHub(IN std::vector<PDODecl> Descs_);
    }
}
