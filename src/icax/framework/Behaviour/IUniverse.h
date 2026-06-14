#pragma once
#include "System.h"
#include "Data/uuid.h"
#include "IWorld.h"
#include "IUniverseContext.h"
#include "../Database/IRepository.h"
#include "Data/PropertyBag.h"
#include "IBehaviourRegistry.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 宇宙
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
            * @brief 获取世界ID
            * @remark 世界ID与DomainID一一对应，即每一个世界都有一个对应得Domain存储其对应得数据
            */
            virtual iCAX::Data::uuid GetID() const = 0;

            /*
            * @brief 帧前交换PDO双缓冲
            */
            virtual void PreSwapPDO() = 0;

            /*
            * @brief 每帧执行
            */
            virtual void Tick() = 0;

            /*
            * @brief 帧后交换PDO双缓冲
            */
            virtual void PostSwapPDO() = 0;

            /*
            * @brief 获取根世界 
            * @return IWorld&
            */
            virtual IWorld& GetRootWorld() = 0;

            /*
            * @brief 获取根世界
            * @return IWorld&
            */
            virtual const IWorld& GetRootWorld() const = 0;

            /*
            * @brief 获取上下文
            * @return IUniverseContext&
            */
            virtual IUniverseContext& GetContext() const = 0;

            /*
            * @brief 清空
            * @param [in] bFaorced_
            */
            virtual void Cleanup(IN const bool& bFaorced_ = false) = 0;

            /*
            * @brief 是否有效
            * @return bool
            */
            virtual bool IsValid() const = 0;
        };

        /*
        * @brief 生成宇宙
        * @param [in] pContext_
        */
        std::shared_ptr<IUniverse> _SYSTEM_EXP GenerateUniverse(IN const std::shared_ptr<iCAX::Database::IRepository>& pRepository_,
            IN const std::shared_ptr<iCAX::Data::PropertyBag>& pApplicationSetting_);
    }
}