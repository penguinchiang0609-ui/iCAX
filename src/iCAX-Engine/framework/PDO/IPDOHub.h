#pragma once
#include "PDO.h"
#include <cstdint>
#include <functional>
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
        * @brief PDOHub 创建参数。
        * @details
        *   PDOHub 创建时只创建一块固定大小的共享内存 Arena。运行期的 PDO slot
        *   由 AllocateSlot 从这块 Arena 上切分出来，并可由 FreeSlot 回收复用。
        */
        struct _PDO_EXP CPDOHubCreateInfo final
        {
            uint64_t nArenaSize = 64ull * 1024ull * 1024ull; //!< 共享内存总大小，单位字节。
            uint32_t nSlotCapacity = 4096; //!< Arena 中最多可同时存在的 slot 数。
            std::vector<PDODecl> InitialDeclarations; //!< 初始分配的 slot，可为空。
        };

        /*
        * @brief 一次碎片整理中被移动的 slot。
        */
        struct _PDO_EXP CPDOHubDefragMove final
        {
            PDOID nPDOID = 0; //!< 被移动的 slot ID。
            uint64_t nOldPayloadBlockOffset = 0; //!< 移动前 payload block offset。
            uint64_t nNewPayloadBlockOffset = 0; //!< 移动后 payload block offset。
            uint64_t nPayloadBlockSize = 0; //!< 被移动的 payload block 字节数。
        };

        /*
        * @brief PDOHub 分配过程回调。
        * @details
        *   PDOHub 只在分配 slot 时内部判断是否需要碎片整理。它不依赖 Context、Facades 或前端，
        *   只在整理开始和整理结束两个时刻调用外部传入的回调。
        *   结束回调携带本次移动的 slot 列表，外部可据此通知前端按 PDOID 重新解析 offset。
        */
        struct _PDO_EXP CPDOHubAllocationCallbacks final
        {
            std::function<void()> OnDefragmentBegin;
            std::function<void(const std::vector<CPDOHubDefragMove>&)> OnDefragmentEnd;
        };
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
            * @brief 判断指定 slot 当前是否存在。
            */
            virtual bool HasSlot(IN const PDOID& nPDOID_) const = 0;

            /*
            * @brief 在 Hub 的共享内存 Arena 上动态分配一个 slot。
            * @param [in] Decl_ slot 声明，包含 PDOID、payload layout 版本、方向和 payload 大小。
            * @return 新分配的 slot。
            * @throws std::invalid_argument 声明非法。
            * @throws std::runtime_error PDOID 重复、slot 表已满或 Arena 剩余空间不足。
            */
            virtual IPDOSlot& AllocateSlot(IN const PDODecl& Decl_) = 0;

            /*
            * @brief 在 Hub 的共享内存 Arena 上动态分配一个 slot，并在内部碎片整理时触发回调。
            * @param [in] Decl_ slot 声明。
            * @param [in] Callbacks_ 分配过程中可选的碎片整理通知回调。
            * @return 新分配的 slot。
            */
            virtual IPDOSlot& AllocateSlot(
                IN const PDODecl& Decl_,
                IN const CPDOHubAllocationCallbacks& Callbacks_) = 0;

            /*
            * @brief 释放一个动态 slot。
            * @return true 表示释放了已有 slot；false 表示指定 PDOID 不存在。
            * @details
            *   释放后旧 IPDOSlot 引用会失效，上层不得继续读写。
            */
            virtual bool FreeSlot(IN const PDOID& nPDOID_) = 0;


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
        * @return 已按声明创建好 Slot 的 Hub。Arena 大小按声明推导，只适合静态或测试场景。
        */
        _PDO_EXP std::shared_ptr<IPDOHub> GeneratePDOHub(IN std::vector<PDODecl> Descs_);

        /*
        * @brief 生成支持运行期动态分配 slot 的 PDO 集线器。
        */
        _PDO_EXP std::shared_ptr<IPDOHub> GeneratePDOHub(IN const CPDOHubCreateInfo& CreateInfo_);
    }
}
