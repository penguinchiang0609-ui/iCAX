#pragma once

#include "ICommandHandler.h"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace iCAX
{
    namespace Command
    {
        /*
        * @brief 命令注册表。
        * @details 以 typeCode 为 key 保存 handler。注册表本身加锁，可被宿主线程和测试线程安全访问。
        */
        class _COMMAND_HANDLER_EXP CCommandRegistry final
        {
        public:
            CCommandRegistry() = default;
            ~CCommandRegistry() = default;

            CCommandRegistry(IN const CCommandRegistry&) = delete;
            CCommandRegistry& operator=(IN const CCommandRegistry&) = delete;

        public:
            /*
            * @brief 注册命令处理器。
            * @return true 表示新增成功；false 表示 typeCode 已存在。
            */
            bool Register(IN uint64_t nTypeCode_, IN std::shared_ptr<ICommandHandler> pHandler_);

            /*
            * @brief 设置命令处理器。
            * @details 如果 typeCode 已存在，则替换旧 handler。
            */
            void Set(IN uint64_t nTypeCode_, IN std::shared_ptr<ICommandHandler> pHandler_);

            /*
            * @brief 注销命令处理器。
            * @return true 表示移除了已有 handler。
            */
            bool Unregister(IN uint64_t nTypeCode_);

            /*
            * @brief 判断命令类型是否已注册。
            */
            bool Has(IN uint64_t nTypeCode_) const;

            /*
            * @brief 查找命令处理器。
            * @return 找到时返回 handler，否则返回 nullptr。
            */
            std::shared_ptr<ICommandHandler> Find(IN uint64_t nTypeCode_) const;

            /*
            * @brief 获取全部已注册命令类型码。
            */
            std::vector<uint64_t> GetTypeCodes() const;

            /*
            * @brief 清空全部处理器。
            */
            void Clear();

        private:
            /*
            * @brief 校验 handler。
            * @throws std::invalid_argument handler 为空时抛出。
            */
            static void ValidateHandler(IN const std::shared_ptr<ICommandHandler>& pHandler_);

        private:
            mutable std::mutex m_Mutex;
            std::map<uint64_t, std::shared_ptr<ICommandHandler>> m_mapHandlers;
        };
    }
}
