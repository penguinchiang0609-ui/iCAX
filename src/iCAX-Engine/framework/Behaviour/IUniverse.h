#pragma once
#include "System.h"
#include "BehaviourBase.h"
#include "Data/uuid.h"
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
        *   Project 在每帧把 Application/Product/Project 三层上下文传入 Universe，
        *   Universe 再调度已绑定行为。
        *   Universe 和其中的 Behaviour 实例按 Project 单线程模型运行，不做内部并发保护；
        *   外部线程应通过 Mailbox/PDO/命令通道把工作交给所属 Project 线程。
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
            IUniverse(IN const IUniverse&) = delete;
            IUniverse(IUniverse&&) = delete;
            IUniverse& operator=(IN const IUniverse&) = delete;
            IUniverse& operator=(IUniverse&&) = delete;

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
            * @param [in] ApplicationContext_ 应用上下文。
            * @param [in] ProductContext_ 产品上下文。
            * @param [in] ProjectContext_ 项目上下文。
            * @param [in] nDeltaTime_ 当前帧间隔秒数，由 Project 运行时调度器提供。
            * @param [in] nTotalTime_ 累计运行秒数，由 Project 运行时调度器提供。
            * @details 应由所属 Project 线程调用；不得与 Repository 事件转发并发进入同一 Universe。
            */
            virtual void Tick(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_) = 0;

            /*
            * @brief 帧后交换PDO双缓冲
            * @details 当前预留给 PDO 帧后交换，通常由 Project 工作线程调用。
            */
            virtual void PostSwapPDO() = 0;

            /*
            * @brief 清空
            * @param [in] bForced_ true 表示强制释放内部调度器。
            */
            virtual void Cleanup(IN const bool& bForced_ = false) = 0;

            /*
            * @brief 是否有效
            * @return bool
            * @details 当前实现以内部 Dispatcher 是否存在作为有效性判断。
            */
            virtual bool IsValid() const = 0;

            /*
            * @brief 外部数据仓储发生变化前的行为通知
            * @param [in] ApplicationContext_ 应用上下文。
            * @param [in] ProductContext_ 产品上下文。
            * @param [in] ProjectContext_ 项目上下文。
            * @param [in] Args_ Repository 事件参数。
            * @details 应由所属 Project 线程在 Repository 事件链路中调用。
            */
            virtual void OnRepositoryChanging(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN const iCAX::Database::RepositoryEventArgs& Args_) = 0;

            /*
            * @brief 外部数据仓储发生变化后的行为通知
            * @param [in] ApplicationContext_ 应用上下文。
            * @param [in] ProductContext_ 产品上下文。
            * @param [in] ProjectContext_ 项目上下文。
            * @param [in] Args_ Repository 事件参数。
            * @details 应由所属 Project 线程在 Repository 事件链路中调用。
            */
            virtual void OnRepositoryChanged(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
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
            * @details 只移除当前 Universe 中的 Behaviour 实例，不注销产品注册表，也不卸载插件 DLL。
            */
            virtual void UnbindBehaviourByIndex(IN const std::type_index& nType_) = 0;

            /*
            * @brief 暂停指定 Behaviour 的帧更新。
            * @param [in] nType_ 行为 C++ 类型。
            * @details
            *   用于仿真等场景临时停止 Behaviour 的帧循环逻辑。
            *   只影响 PreUpdate/Update/PostUpdate，不拦截 Start/Awake/Enable/Disable/Destroy/Modifying/Modified。
            */
            virtual void PauseFrameUpdateByIndex(IN const std::type_index& nType_) = 0;

            /*
            * @brief 指定 Behaviour 是否已暂停帧更新。
            * @return true 表示帧更新已暂停。
            */
            virtual bool IsFrameUpdatePausedByIndex(IN const std::type_index& nType_) const = 0;

            /*
            * @brief 恢复指定 Behaviour 的帧更新。
            * @param [in] nType_ 行为 C++ 类型。
            */
            virtual void ResumeFrameUpdateByIndex(IN const std::type_index& nType_) = 0;

            /*
            * @brief 获取已暂停帧更新的行为列表。
            * @return 已暂停帧更新的行为实例列表。
            */
            virtual std::vector<std::shared_ptr<CBehaviourBase>> GetFrameUpdatePausedBehaviours() const = 0;

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
                    return false;
                }
                return BindBehaviourByIndex(_nTypeIndex);
            }

            template<typename T>
            void PauseFrameUpdate()
            {
                PauseFrameUpdateByIndex(std::type_index(typeid(T)));
            }

            template<typename T>
            bool IsFrameUpdatePaused() const
            {
                return IsFrameUpdatePausedByIndex(std::type_index(typeid(T)));
            }

            template<typename T>
            void ResumeFrameUpdate()
            {
                ResumeFrameUpdateByIndex(std::type_index(typeid(T)));
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
        std::shared_ptr<IUniverse> _SYSTEM_EXP GenerateUniverse(IN std::shared_ptr<IBehaviourRegistry> pRegistry_);
    }
}
