#pragma once

#include "CommandTarget.h"

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
        * @details
        *   以主命令码为 key 保存命令目标。每个命令目标内部再管理子命令。
        *   注册表本身加锁，可被宿主线程和测试线程安全访问。
        *   注册表不提供替换和注销接口；命令协议在 runtime 启动后应保持稳定。
        */
        class _COMMAND_TARGETS_EXP CCommandRegistry final
        {
        public:
            CCommandRegistry() = default;
            ~CCommandRegistry() = default;

            CCommandRegistry(IN const CCommandRegistry&) = delete;
            CCommandRegistry& operator=(IN const CCommandRegistry&) = delete;

        public:
            /*
            * @brief 注册命令目标。
            * @return true 表示新增成功；false 表示同一主命令已存在。
            * @throws std::runtime_error 主命令码与不同名称发生碰撞。
            */
            bool Register(IN std::shared_ptr<ICommandTarget> pCommandTarget_);

            /*
            * @brief 判断主命令是否已注册。
            */
            bool Has(IN uint32_t nMainCode_) const;

            /*
            * @brief 查找命令目标。
            * @return 找到时返回命令目标，否则返回 nullptr。
            */
            std::shared_ptr<ICommandTarget> Find(IN uint32_t nMainCode_) const;

            /*
            * @brief 获取全部已注册主命令码。
            */
            std::vector<uint32_t> GetMainCodes() const;

            /*
            * @brief 获取全部已注册命令路由。
            * @return 已注册的主/子命令路由快照，用于诊断、日志和前端协议清单。
            */
            std::vector<CCommandRoute> GetCommandRoutes() const;

        private:
            /*
            * @brief 校验命令目标。
            * @throws std::invalid_argument 命令目标为空或主命令码为空时抛出。
            */
            static void ValidateCommandTarget(IN const std::shared_ptr<ICommandTarget>& pCommandTarget_);

            /*
            * @brief 检查主命令码碰撞。
            */
            static void ValidateMainCommandCollision(
                IN const ICommandTarget& ExistingTarget_,
                IN const ICommandTarget& NewTarget_);

        private:
            mutable std::mutex m_Mutex;
            std::map<uint32_t, std::shared_ptr<ICommandTarget>> m_mapCommandTargets;
        };
    }
}
