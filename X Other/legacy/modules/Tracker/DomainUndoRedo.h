#pragma once
#include "Data/uuid.h"
#include <stack>
#include <list>
#include "../Database/IRepositoryEvent.h"
#include "../Database/Domain.h"
#include <deque>

namespace iCAX
{
    namespace Tracker
    {
        /*
        * @brief 撤销还原步
        */
        struct UROperationStep final
        {
            iCAX::Data::uuid ID;
            std::string strName;
            std::list<iCAX::Data::uuid> lstDomain;
            std::list<iCAX::Database::OperationUnit> Operation;
        };

        /*
        * @brief 域撤销还原
        * @remark 每个域拥有自己的撤销还原堆栈
        */
        class CDomainUndoRedo final
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] pDomain_
            */
            CDomainUndoRedo(IN std::shared_ptr<iCAX::Database::IDomain> pDomain_);

            /*
            * @brief 析构函数
            */
            virtual ~CDomainUndoRedo();

        public:
            /*
            * @brief 追加还原
            * @param [in] pStep_
            */
            void PushUndo(IN std::shared_ptr<UROperationStep> pStep_);
            /*
            * @brief 开始记录撤销还原
            * @param [in] strName_ 步骤名称
            * @return bool 操作是否成功
            */
            bool BeginStep(const std::string& strName_);

            /*
            * @brief 停止撤销还原
            * @return std::shared_ptr<CURStep> 撤销还原步
            */
            std::weak_ptr<UROperationStep> EndStep();

            /*
            * @brief 是否正在记录
            * @return bool
            */
            bool IsRecording() const;

            /*
            * @brief 是否可以重做
            * @return bool
            */
            bool CanRedo() const;

            /*
            * @brief 是否可以撤销
            * @return bool
            */
            bool CanUndo() const;

            /*
            * @brief 重做
            * @param [in] bFake_
            * @return bool
            */
            void Redo(IN const bool& bFake_ = false);

            /*
            * @brief 撤销
            * @param [in] bFake_
            * @return bool
            */
            void Undo(IN const bool& bFake_ = false);

            /*
            * @brief 获取实例ID
            * @return CGuid
            */
            iCAX::Data::uuid GetDomainID() const;

            /*
            * @brief 获取当前操作列表
            * @rturn const std::list<COperationUnit>&
            */
            const std::list<iCAX::Database::OperationUnit>& GetCurrentOperations() const;

            /*
            * @brief 获取当前操作列表
            * @rturn std::list<COperationUnit>&
            */
            std::list<iCAX::Database::OperationUnit>& GetCurrentOperations();

            /*
            * @brief 获取栈顶撤销
            * @return const CURStep&
            */
            std::weak_ptr<UROperationStep> PeekUndo() const;

            /*
            * @brief 获取栈顶重做
            * @retun const CURStep&
            */
            std::weak_ptr<UROperationStep> PeekRedo() const;

            /*
            * @brief 批量记录
            * @param [in] Operations_
            */
            void BatchRecord(IN const std::list<iCAX::Database::OperationUnit>& Operations_);

            /*
            * @brief 操作后事件
            * @param [in] Operation_
            */
            void Record(IN const iCAX::Database::OperationUnit& Operation_);

            /*
            * @brief 获取撤销步骤列表
            * @return std::vector<std::tuple<iCAX::Data::uuid, std::string>>
            */
            std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetUndoArray() const;

            /*
            * @brief 获取重做步骤列表
            * @return std::vector<std::tuple<iCAX::Data::uuid, std::string>>
            */
            std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetRedoArray() const;

        private:
            std::weak_ptr<iCAX::Database::IDomain> m_pDomain;                           //!< 域
            std::string m_strCurrStepName;                                              //!< 名称
            std::list<iCAX::Database::OperationUnit> m_CurrOperationUnits;              //!< 当前操作单元
            std::deque<std::shared_ptr<UROperationStep>> m_UndoStack;                   //!< 还原堆栈
            std::deque<std::shared_ptr<UROperationStep>> m_RedoStack;                   //!< 重做堆栈
        };
    }
}