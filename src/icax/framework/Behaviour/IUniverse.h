#pragma once
#include "System.h"
#include "BehaviourBase.h"
#include "Data/uuid.h"
#include "IUniverseContext.h"
#include "../Database/IRepositoryEvent.h"
#include <memory>
#include <typeindex>
#include <vector>

namespace iCAX
{
    namespace Behaviour
    {
        class IBehaviourRegistry;

        /*
        * @brief 宇宙
        * @remark
        *   Universe 是行为运行容器，不拥有 Repository，也不代表 Project。
        *   Project 在每帧把自己的 UniverseContext 传入 Universe，Universe 再调度已绑定行为。
        */
        class _SYSTEM_EXP IUniverse
        {
        public:
            /*
            * @brief 构造函数
            */
            IUniverse() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IUniverse() = default;

            //!< 禁用
            IUniverse(IUniverse&) = delete;
            IUniverse(IUniverse&&) = delete;
            IUniverse* operator=(IUniverse&) = delete;
            IUniverse* operator=(IUniverse&&) = delete;

        public:
            /*
            * @brief 获取运行容器ID
            * @remark Universe 本身不持有数据；该 ID 只用于标识行为运行容器实例。
            */
            virtual iCAX::Data::uuid GetID() const = 0;

            /*
            * @brief 帧前交换PDO双缓冲
            * @details 当前预留给 PDO 帧前交换，通常由 Project 工作线程调用。
            */
            virtual void PreSwapPDO() = 0;

            /*
            * @brief 每帧执行
            * @param [in] Context_ 本帧运行上下文，包含 Repository、Timer 和应用设置。
            */
            virtual void Tick(IN const IUniverseContext& Context_) = 0;

            /*
            * @brief 帧后交换PDO双缓冲
            * @details 当前预留给 PDO 帧后交换，通常由 Project 工作线程调用。
            */
            virtual void PostSwapPDO() = 0;

            /*
            * @brief 清空
            * @param [in] bFaorced_ true 表示强制释放内部调度器。
            */
            virtual void Cleanup(IN const bool& bFaorced_ = false) = 0;

            /*
            * @brief 是否有效
            * @return bool
            * @details 当前实现以内部 Dispatcher 是否存在作为有效性判断。
            */
            virtual bool IsValid() const = 0;

            /*
            * @brief 外部数据仓储发生变化前的行为通知
            * @param [in] Context_ Universe 上下文。
            * @param [in] Args_ Repository 事件参数。
            */
            virtual void OnRepositoryChanging(
                IN const IUniverseContext& Context_,
                IN const iCAX::Database::RepositoryEventArgs& Args_) = 0;

            /*
            * @brief 外部数据仓储发生变化后的行为通知
            * @param [in] Context_ Universe 上下文。
            * @param [in] Args_ Repository 事件参数。
            */
            virtual void OnRepositoryChanged(
                IN const IUniverseContext& Context_,
                IN const iCAX::Database::RepositoryEventArgs& Args_) = 0;

            /*
            * @brief 绑定行为类型
            * @param [in] nType_ 行为 C++ 类型。
            * @return true 表示本次新增绑定；false 表示已绑定。
            */
            virtual bool BindBehaviourByIndex(IN const std::type_index& nType_) = 0;

            /*
            * @brief 判断行为是否已经绑定
            * @return true 表示已绑定。
            */
            virtual bool HasBindBehaviourByIndex(IN const std::type_index& nType_) const = 0;

            /*
            * @brief 解除行为绑定
            * @param [in] nType_ 行为 C++ 类型。
            */
            virtual void UnbindBehaviourByIndex(IN const std::type_index& nType_) = 0;

            /*
            * @brief 暂停指定行为
            * @param [in] nType_ 行为 C++ 类型。
            */
            virtual void PauseBehaviourByIndex(IN const std::type_index& nType_) = 0;

            /*
            * @brief 指定行为是否暂停
            * @return true 表示已暂停。
            */
            virtual bool IsBehaviourPausedByIndex(IN const std::type_index& nType_) const = 0;

            /*
            * @brief 恢复指定行为
            * @param [in] nType_ 行为 C++ 类型。
            */
            virtual void ResumeBehaviourByIndex(IN const std::type_index& nType_) = 0;

            /*
            * @brief 获取暂停的行为列表
            * @return 已暂停行为实例列表。
            */
            virtual std::vector<std::shared_ptr<CBehaviourBase>> GetPausedBehaviours() const = 0;

            /*
            * @brief 获取所有绑定的行为列表
            * @return 已绑定行为实例列表。
            */
            virtual std::vector<std::shared_ptr<CBehaviourBase>> GetAllBehaviours() const = 0;

            template<typename T>
            bool BindBehaviour()
            {
                auto _nTypeIndex = std::type_index(typeid(T));
                if (HasBindBehaviourByIndex(_nTypeIndex))
                {
                    return true;
                }
                return BindBehaviourByIndex(_nTypeIndex);
            }

            template<typename T>
            void PauseBehaviour()
            {
                PauseBehaviourByIndex(std::type_index(typeid(T)));
            }

            template<typename T>
            bool IsBehaviourPaused() const
            {
                return IsBehaviourPausedByIndex(std::type_index(typeid(T)));
            }

            template<typename T>
            void ResumeBehaviour()
            {
                ResumeBehaviourByIndex(std::type_index(typeid(T)));
            }

            template<typename T>
            bool HasBindBehaviour() const
            {
                return HasBindBehaviourByIndex(std::type_index(typeid(T)));
            }

            template<typename T>
            void UnbindBehaviour()
            {
                UnbindBehaviourByIndex(std::type_index(typeid(T)));
            }
        };

        /*
        * @brief 生成宇宙
        */
        std::shared_ptr<IUniverse> _SYSTEM_EXP GenerateUniverse();
        std::shared_ptr<IUniverse> _SYSTEM_EXP GenerateUniverse(IN std::shared_ptr<IBehaviourRegistry> pRegistry_);
    }
}
