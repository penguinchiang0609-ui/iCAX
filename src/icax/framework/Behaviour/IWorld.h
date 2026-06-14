#pragma once
#include "System.h"
#include "Data/uuid.h"
#include "Database/IDomain.h"
#include "BehaviourBase.h"
#include "IUniverseContext.h"
#include "PDO/IPDOHub.h"
#include "IWorldContext.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 世界接口
        */
        class _SYSTEM_EXP IWorld
        {
        public:
            enum NotifyType
            {
                kAddComponent = 0,
                kEnableComponent,
                kDisableComponent,
                kDestroyComponent,
                kModifingComponent,
                kModifiedComponent
            };

        public:
            /*
            * @brief 构造函数
            */
            IWorld() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IWorld() = default;

            //!< 禁用
            IWorld(IWorld&) = delete;
            IWorld(IWorld&&) = delete;
            IWorld* operator=(IWorld&) = delete;
            IWorld* operator=(IWorld&&) = delete;

        public:
            /*
            * @brief 获取世界ID
            * @remark 世界ID与DomainID一一对应，即每一个世界都有一个对应得Domain存储其对应得数据
            */
            virtual iCAX::Data::uuid GetID() const = 0;

        public:
            /*
            * @brief 初始化PDO
            * @param [in] Descs_
            */
            virtual void InitialziePDO(IN std::vector<iCAX::PDO::PDODecl> Descs_) = 0;

            /*
            * @brief 含有PDO
            * @return bool
            */
            virtual bool HasPDO() const = 0;

            /*
            * @brief 获取PDO槽集线器
            * @return iCAX::PDO::PDOHub&
            */
            virtual iCAX::PDO::IPDOHub& GetPDOHub() = 0;

            /*
            * @brief 帧前交换PDO双缓冲
            */
            virtual void PreSwapPDO() = 0;

            /*
            * @brief 帧后交换PDO双缓冲
            */
            virtual void PostSwapPDO() = 0;

#pragma region

            /*
            * @brief 创建新的子世界
            * @param [in] ID_
            * @return IWorld&
            */
            virtual IWorld& CreateChild(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 获取子世界
            * @param [in] ID_
            * @return std::shared_ptr<IWorld>
            */
            virtual std::shared_ptr<IWorld> GetChild(IN const iCAX::Data::uuid& ID_) const = 0;

            /*
            * @brief 获取子世界
            * @return std::vector<iCAX::Data::uuid>
            */
            virtual std::vector<iCAX::Data::uuid> GetChildren() = 0;

            /*
            * @brief 移除子世界
            * @param [in] ID_
            */
            virtual void DestoryChild(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 获取所在的宇宙
            * @return std::shared_ptr<class IUniverse>
            */
            virtual std::shared_ptr<class IUniverse> GetUniverse() const = 0;

            /*
            * @brief 获取实体域
            * @return std::shared_ptr<iCAX::Database::IDomain>
            */
            virtual std::shared_ptr<iCAX::Database::IDomain> GetDomain() const = 0;

            /*
            * @brief 获取父世界
            * @return std::shared_ptr<IWorld>
            */
            virtual std::shared_ptr<IWorld> GetParentWorld() const = 0;

            /*
            * @brief 清空
            */
            virtual void Cleanup() = 0;

#pragma endregion

#pragma region

            /*
            * @brief 每一帧执行一次
            * @param [in] nDeltaTime_ 距离上一帧的时间
            * @param [in] nTotalTime_ 程序运行总时长
            */
            virtual void Tick(IN const IUniverseContext& Context_, IN const double& nDeltaTime_, IN const double& nTotalTime_) = 0;

            /*
            * @brief 暂停
            * @param [in] bIncludeAllChildren_
            * @remark 暂停后，ontick方法将不被触发
            */
            virtual void Pause(IN const bool& bIncludeAllChildren_) = 0;

            /*
            * @brief 从暂停中恢复
            * @param [in] bIncludeAllChildren_
            */
            virtual void Resume(IN const bool& bIncludeAllChildren_) = 0;

            /*
            * @brief 获取是否暂停
            * @return bool
            */
            virtual bool IsPaused() const = 0;

#pragma endregion

#pragma region

            /*
            * @brief 注册Behaviour
            * @param [in] pBehaviour_
            * @return bool
            */
            virtual bool BindBehaviourByIndex(IN const std::type_index& nType_) = 0;

            /*
            * @brief 是否已包含
            * @param [in] nType_
            * @return bool
            */
            virtual bool HasBindBehaviourByIndex(IN const std::type_index& nType_) const = 0;

            /*
            * @brief 注销行为
            * @param [in] nType_
            */
            virtual void UnbindBehaviourByIndex(IN const std::type_index& nType_) = 0;

            /*
            * @brief 暂停指定行为
            * @param [in] nType_
            */
            virtual void PauseBehaviourByIndex(IN const std::type_index& nType_) = 0;

            /*
            * @brief 指定行为是否被暂停
            * @param [in] nType_
            * @return bool
            */
            virtual bool IsBehaviourPausedByIndex(IN const std::type_index& nType_) const = 0;

            /*
            * @brief 恢复指定行为
            * @param [in] nType_
            */
            virtual void ResumeBehaviourByIndex(IN const std::type_index& nType_) = 0;

            /*
            * @brief 获取暂停的行为列表
            * @return std::vector<std::shared_ptr<CBehaviourBase>>
            */
            virtual std::vector<std::shared_ptr<CBehaviourBase>> GetPausedBehaviours() const = 0;

            /*
            * @brief 获取所有绑定的行为列表
            * @return std::vector<std::shared_ptr<CBehaviourBase>>
            */
            virtual std::vector<std::shared_ptr<CBehaviourBase>> GetAllBehaviours() const = 0;

            /*
            * @brief 注册Behaviour
            */
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

            /*
            * @brief 暂停指定行为
            */
            template<typename T>
            void PauseBehaviour()
            {
                auto _nTypeIndex = std::type_index(typeid(T));
                PauseBehaviourByIndex(_nTypeIndex);
            }

            /*
            * @brief 指定行为是否暂停
            */
            template<typename T>
            bool IsBehaviourPaused()
            {
                auto _nTypeIndex = std::type_index(typeid(T));
                return IsBehaviourPausedByIndex(_nTypeIndex);
            }

            /*
            * @brief 暂停指定行为
            */
            template<typename T>
            void ResumeBehaviour()
            {
                auto _nTypeIndex = std::type_index(typeid(T));
                ResumeBehaviourByIndex(_nTypeIndex);
            }

            /*
            * @brief 注销Behaviour
            */
            template<typename T>
            void HasBindBehaviour()
            {
                return HasBindBehaviourByIndex(std::type_index(typeid(T)));
            }

            /*
            * @brief 注销Behaviour
            */
            template<typename T>
            void UnbindBehaviour()
            {
                UnbindBehaviourByIndex(std::type_index(typeid(T)));
            }

#pragma endregion
            /*
            * @brief 获取世界运行时上下文
            * @return IWorldContext&
            * @remark 此字段慎用，严禁滥用该字段存储各种过程变量与状态!!!!!!!!!!!!!!!!!!!!!!!!!!
            *   使用此字段必须向主管报备!!!!!!!!!!!!!!!!!!!!!!!!!!
            */
            virtual IWorldContext& GetContext() = 0;
        };
    }
}
