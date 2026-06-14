#pragma once
#include "IUniverse.h"
#include <chrono>
#include "../Database/IRepositoryEvent.h"
#include "World.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 宇宙
        * @remark 
        *   1、相当于Project，即一个项目
        *   2、应用程序本身的配置信息放置在ApplicationSetting中
        */
        class CUniverse final : public IUniverse, public iCAX::Database::IRepositoryEventListener, public std::enable_shared_from_this<CUniverse>
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] pContext_
            */
            CUniverse(IN std::shared_ptr<IUniverseContext> pContext_);

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
            virtual void Tick() override;

            /*
            * @brief 帧后交换PDO双缓冲
            */
            virtual void PostSwapPDO() override;

            /*
            * @brief 获取根世界
            * @return IWorld&
            */
            virtual IWorld& GetRootWorld() override;

            /*
            * @brief 获取根世界
            * @return IWorld&
            */
            virtual const IWorld& GetRootWorld() const override;

            /*
            * @brief 获取环境
            * @return IUniverseContext&
            */
            virtual IUniverseContext& GetContext() const override;

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
            * @param [in] pSender_
            * @param [in] Args_
            */
            virtual void OnRepositoryChanging(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_) override;

            /*
            * @brief 更改后事件
            * @param [in] pSender_
            * @param [in] Args_
            */
            virtual void OnRepositoryChanged(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_) override;

        private:
            std::shared_ptr<IUniverseContext> m_pContext;                //!< 上下文
            std::shared_ptr<CWorld> m_pRootWorld;                           //!< 根世界
        };
    }
}