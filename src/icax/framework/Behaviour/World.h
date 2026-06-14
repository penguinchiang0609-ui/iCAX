#pragma once

#include "Data/uuid.h"
#include "IWorld.h"
#include "IUniverse.h"
#include "BehaviourDispacther.h"
#include "IUniverseContext.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 世界
        */
        class CWorld final : public IWorld, public std::enable_shared_from_this<CWorld>
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] pUniverse_
            * @param [in] ID_
            */
            CWorld(std::weak_ptr<IUniverse> pUniverse_, const iCAX::Data::uuid& ID_);

            /*
            * @brief 构造函数
            * @param [in] pParent_
            * @param [in] ID_
            */
            CWorld(std::weak_ptr<CWorld> pParent_, const iCAX::Data::uuid& ID_);

            /*
            * @brief 析构函数
            */
            virtual ~CWorld();

        public:
            /*
            * @brief 获取世界ID
            * @remark 世界ID与DomainID一一对应，即每一个世界都有一个对应得Domain存储其对应得数据
            */
            virtual iCAX::Data::uuid GetID() const override;

#pragma region
            //!--------------PDO管理---------------
        public:
            /*
            * @brief 初始化PDO
            * @param [in] Descs_
            */
            virtual void InitialziePDO(IN std::vector<iCAX::PDO::PDODecl> Descs_) override;

            /*
            * @brief 是否含有PDO
            * @return bool
            */
            virtual bool HasPDO() const override;

            /*
            * @brief 获取PDO槽集线器
            * @return iCAX::PDO::PDOHub&
            */
            virtual iCAX::PDO::IPDOHub& GetPDOHub() override;

            /*
            * @brief 帧前交换PDO双缓冲
            */
            virtual void PreSwapPDO() override;

            /*
            * @brief 帧后交换PDO双缓冲
            */
            virtual void PostSwapPDO() override;

#pragma endregion

#pragma region
            //!------------子世界管理----------------
        public:
            /*
            * @brief 创建新的子世界
            * @param [in] ID_
            * @return std::weak_ptr<CWorld>
            */
            virtual IWorld& CreateChild(IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 获取子世界
            * @param [in] ID_
            * @return std::shared_ptr<IWorld>
            */
            virtual std::shared_ptr<IWorld> GetChild(IN const iCAX::Data::uuid& ID_) const override;

            /*
            * @brief 获取子世界
            * @return std::vector<iCAX::Data::uuid>
            */
            virtual std::vector<iCAX::Data::uuid> GetChildren() override;

            /*
            * @brief 移除子世界
            * @param [in] ID_
            */
            virtual void DestoryChild(IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 获取所在的宇宙
            * @return std::shared_ptr<IUniverse>
            */
            virtual std::shared_ptr<IUniverse> GetUniverse() const override;

            /*
            * @brief 获取实体域
            * @return std::shared_ptr<iCAX::Database::IDomain>
            */
            virtual std::shared_ptr<iCAX::Database::IDomain> GetDomain() const override;

            /*
            * @brief 获取父世界
            * @return std::shared_ptr<IWorld>
            */
            virtual std::shared_ptr<IWorld> GetParentWorld() const override;

            /*
            * @brief 清空
            */
            virtual void Cleanup() override;

#pragma endregion

#pragma region

            /*
            * @brief 每一帧执行一次
            * @param [in] nDeltaTime_ 距离上一帧的时间
            * @param [in] nTotalTime_ 程序运行总时长
            */
            virtual void Tick(IN const IUniverseContext& Context_, IN const double& nDeltaTime_, IN const double& nTotalTime_) override;

            /*
            * @brief 暂停
            * @param [in] bIncludeAllChildren_
            * @remark 暂停后，ontick方法将不被触发
            */
            virtual void Pause(IN const bool& bIncludeAllChildren_) override;

            /*
            * @brief 从暂停中恢复
            * @param [in] bIncludeAllChildren_
            */
            virtual void Resume(IN const bool& bIncludeAllChildren_) override;

            /*
            * @brief 获取是否暂停
            * @return bool
            */
            virtual bool IsPaused() const override;

#pragma endregion

#pragma region

            /*
            * @brief 注册Behaviour
            * @param [in] nType_
            * @return bool
            */
            virtual bool BindBehaviourByIndex(IN const std::type_index& nType_) override;

            /*
            * @brief 是否已包含
            * @param [in] nType_
            * @return bool
            */
            virtual bool HasBindBehaviourByIndex(IN const std::type_index& nType_) const override;

            /*
            * @brief 注销行为
            * @param [in] nType_
            */
            virtual void UnbindBehaviourByIndex(IN const std::type_index& nType_) override;

            /*
            * @brief 暂停指定行为
            * @param [in] nType_
            */
            virtual void PauseBehaviourByIndex(IN const std::type_index& nType_) override;

            /*
            * @brief 指定行为是否被暂停
            * @param [in] nType_
            * @return bool
            */
            virtual bool IsBehaviourPausedByIndex(IN const std::type_index& nType_) const override;

            /*
            * @brief 恢复指定行为
            * @param [in] nType_
            */
            virtual void ResumeBehaviourByIndex(IN const std::type_index& nType_) override;

            /*
            * @brief 获取暂停的行为列表
            */
            virtual std::vector<std::shared_ptr<CBehaviourBase>> GetPausedBehaviours() const override;

            /*
            * @brief 获取所有绑定的行为列表
            * @return std::unordered_set<std::shared_ptr<CBehaviourBase>>
            */
            virtual std::vector<std::shared_ptr<CBehaviourBase>> GetAllBehaviours() const override;

#pragma endregion

        public:
            /*
            * @brief 仓储层发生变化前
            * @param [in] nType_
            * @param [in] nDomain_
            * @param [in] pComponent_
            * @param [in] Properties_
            */
            void OnRepositoryChanging(IN const IUniverseContext& Context_, IN const iCAX::Database::RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& nDomain_, IN const std::shared_ptr<iCAX::Database::CComponentBase> pComponent_, IN const iCAX::Data::PropertySet& Properties_);

            /*
            * @brief 仓储层发生变化后
            * @param [in] nType_
            * @param [in] nDomain_
            * @param [in] pComponent_
            * @param [in] Properties_
            */
            void OnRepositoryChanged(IN const IUniverseContext& Context_, IN const iCAX::Database::RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& nDomain_, IN const std::shared_ptr<iCAX::Database::CComponentBase> pComponent_, IN const iCAX::Data::PropertySet& Properties_);

            /*
            * @brief 获取世界运行时上下文
            * @return IWorldContext&
            */
            virtual IWorldContext& GetContext() override;

        private:
            iCAX::Data::uuid m_ID;                                                //!< 世界ID，该ID对应仓储中的DomainID
            bool m_bPause;
            std::weak_ptr<IUniverse> m_pUniverse;                                   //!< 所属的宇宙
            std::weak_ptr<CWorld> m_pParent;                                        //!< 父世界
            std::vector<std::shared_ptr<CWorld>> m_Children;                        //!< 子世界
            std::shared_ptr<CBehaviourDispatcher> m_BehaviourDispatcher;                //!< 行为调度者
            std::shared_ptr<iCAX::PDO::IPDOHub> m_PDOHub;
            std::shared_ptr<IWorldContext> m_Context;
        };
    }
}