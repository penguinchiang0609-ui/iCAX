#pragma once
#include "IUniverse.h"
#include <chrono>
#include "BehaviourDispacther.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 宇宙
        * @remark 
        *   Universe 是行为运行容器，不拥有 Repository，也不代表 Project。
        */
        class CUniverse final : public IUniverse, public std::enable_shared_from_this<CUniverse>
        {
        public:
            /*
            * @brief 构造函数
            */
            CUniverse();

            /*
            * @brief 析构函数
            */
            virtual ~CUniverse();

        public:
            /*
            * @brief 初始化
            */
            void Initialize();

            /*
            * @brief 获取宇宙ID
            * @return iCAX::Data::uuid
            */
            virtual iCAX::Data::uuid GetID() const override;

            /*
            * @brief 帧前交换PDO双缓冲
            */
            virtual void PreSwapPDO() override;

            /*
            * @brief 每帧执行
            */
            virtual void Tick(IN const IUniverseContext& Context_) override;

            /*
            * @brief 帧后交换PDO双缓冲
            */
            virtual void PostSwapPDO() override;

            /*
            * @brief 清空
            * @param [in] bFaorced_
            */
            virtual void Cleanup(IN const bool& bFaorced_ = false) override;

            /*
            * @brief 是否有效
            * @return bool
            */
            virtual bool IsValid() const override;

        public:
            /*
            * @brief 修改前事件
            * @param [in] Args_
            */
            virtual void OnRepositoryChanging(
                IN const IUniverseContext& Context_,
                IN const iCAX::Database::RepositoryEventArgs& Args_) override;

            /*
            * @brief 更改后事件
            * @param [in] Args_
            */
            virtual void OnRepositoryChanged(
                IN const IUniverseContext& Context_,
                IN const iCAX::Database::RepositoryEventArgs& Args_) override;

            virtual bool BindBehaviourByIndex(IN const std::type_index& nType_) override;
            virtual bool HasBindBehaviourByIndex(IN const std::type_index& nType_) const override;
            virtual void UnbindBehaviourByIndex(IN const std::type_index& nType_) override;
            virtual void PauseBehaviourByIndex(IN const std::type_index& nType_) override;
            virtual bool IsBehaviourPausedByIndex(IN const std::type_index& nType_) const override;
            virtual void ResumeBehaviourByIndex(IN const std::type_index& nType_) override;
            virtual std::vector<std::shared_ptr<CBehaviourBase>> GetPausedBehaviours() const override;
            virtual std::vector<std::shared_ptr<CBehaviourBase>> GetAllBehaviours() const override;

        private:
            iCAX::Data::uuid m_ID;
            std::shared_ptr<CBehaviourDispatcher> m_pDispatcher;
        };
    }
}
