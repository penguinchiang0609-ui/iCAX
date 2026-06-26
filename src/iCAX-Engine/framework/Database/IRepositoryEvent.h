#pragma once
#include "Database.h"
#include <string>
#include "Data/Variant.h"
#include "Data/uuid.h"
#include <map>
#include <memory>
#include <vector>

using namespace iCAX::Data;

namespace iCAX
{
    namespace Database
    {
        struct COperationBatch;
        struct RepositoryEventBatch;

        /*
        * @brief 仓储事件参数
        */
        struct _DATABASE_EXP RepositoryEventArgs
        {
            enum EventType
            {
                kModifyComponent = 0,                   //!< 修改组件
                kEnableComponent,                       //!< 启用组件
                kDisableComponent,                      //!< 禁用组件
                kAddComponent,                          //!< 增加组件
                kRemoveComponent,                       //!< 移除组件
                kAddEntity,                             //!< 增加实体
                kDeleteEntity,                          //!< 删除实体
                kBatchChanged,                          //!< 批量变更
            }nType = kModifyComponent;                                                 //!< 操作类型，即对于组件的操作
            iCAX::Data::uuid RepositoryID;                        //!< 仓储ID
            iCAX::Data::uuid EntityID;                            //!< 实体ID
            std::string strClassName;                               //!< 组件类型
            //!< add时，旧值为空，新值为组件的值
            //!< remove时，旧值为组件的值，新值为空
            //!< modify时，旧值为对应字段原来的值，新值为对应字段后来的值
            //!< replace时，旧值为组件原来的值，新值为组件后来的值
            PropertySet PreviousProperties;                         //!< 旧值
            PropertySet NewProperties;                              //!< 新值 

            std::shared_ptr<class CComponentBase> pComponent;       //!< 组件
            std::shared_ptr<class IEntity> pEntity;                 //!< 实体
            std::shared_ptr<class IRepository> pRepository;         //!< 仓储
            std::shared_ptr<const RepositoryEventBatch> pBatch;     //!< 批量事件明细，仅 kBatchChanged 使用。
        };

        /*
        * @brief 批量事件中的单条原始事件记录。
        * @details
        *   与 OperationBatch 不同，RepositoryEventRecord 可以保存运行期组件指针，
        *   只用于本进程通知，不参与历史、快速保存或持久化。
        */
        struct _DATABASE_EXP RepositoryEventRecord final
        {
            RepositoryEventArgs::EventType nType = RepositoryEventArgs::kModifyComponent;
            iCAX::Data::uuid EntityID;
            std::string strClassName;
            PropertySet PreviousProperties;
            PropertySet NewProperties;
            std::shared_ptr<class CComponentBase> pComponent;
            std::shared_ptr<class IEntity> pEntity;
        };

        /*
        * @brief 批量事件明细。
        */
        struct _DATABASE_EXP RepositoryEventBatch final
        {
            std::shared_ptr<const COperationBatch> pOperationBatch; //!< 有序事实日志批次。
            std::vector<RepositoryEventRecord> Records;            //!< 原始事件记录，保留运行期组件指针。
        };

        /*
        * @brief 仓储事件侦听者
        */
        class _DATABASE_EXP IRepositoryEventListener
        {
        public:
            /*
            * @brief 构造函数
            */
            IRepositoryEventListener() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IRepositoryEventListener() = default;

        public:
            /*
            * @brief 修改前事件
            * @param [in] pSender_
            * @param [in] Args_
            */
            virtual void OnRepositoryChanging(IN void* pSender_, IN const RepositoryEventArgs& Args_) = 0;

            /*
            * @brief 更改后事件
            * @param [in] pSender_
            * @param [in] Args_
            */
            virtual void OnRepositoryChanged(IN void* pSender_, IN const RepositoryEventArgs& Args_) = 0;
        };

        /*
        * @brief 仓储事件发布者
        */
        class _DATABASE_EXP IRepositoryEventPublisher
        {
        public:
            /*
            * @brief 构造函数
            */
            IRepositoryEventPublisher() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IRepositoryEventPublisher() = default;

        public:
            /*
            * @brief 添加观察者
            * @param [in] Observer_
            */
            virtual void AddObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_) = 0;

            /*
            * @brief 移除观察者
            * @param [in] Observer_
            */
            virtual void RemoveObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_) = 0;
        };

        typedef RepositoryEventArgs OperationUnit;
        typedef IRepositoryEventListener IOperationListener;
        typedef IRepositoryEventPublisher IOperationPublisher;
    }
}
