#pragma once
#include "PDO.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
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
            * @brief 获取 Hub 对应的共享内存名称。
            * @return Windows file mapping 名称；未初始化时返回空字符串。
            * @details
            *   前端宿主桥接层应通过该名称打开同一块 shared memory，
            *   不应跨进程传递当前进程的内存地址。
            */
            virtual const std::wstring& GetSharedArenaName() const noexcept = 0;

            /*
            * @brief 获取 Hub 对应的共享内存大小。
            * @return Arena 字节数；未初始化时返回 0。
            */
            virtual uint64_t GetSharedArenaSize() const noexcept = 0;

            /*
            * @brief 获取 Hub 当前 PDO 声明快照。
            * @return PDO 声明列表；未初始化时返回空列表。
            */
            virtual std::vector<PDODecl> GetPDODeclarations() const = 0;

            /*
            * @brief 交换入槽
            * @details 交换方向为 kDirection2Inner 的槽，通常在每帧开始调用。
            */
            virtual void SwapInSlot() = 0;

            /*
            * @brief 交换出槽
            * @details 交换方向为 kDirection2External 的槽，通常在每帧结束调用。
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
