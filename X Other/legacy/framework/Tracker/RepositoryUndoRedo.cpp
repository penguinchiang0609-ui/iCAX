#include "pch.h"
#include "RepositoryUndoRedo.h"

//!< 构造函数
iCAX::Tracker::CRepositoryUndoRedo::CRepositoryUndoRedo(IN std::shared_ptr<iCAX::Database::IRepository> pRepository_)
    : m_pRepository(pRepository_)
    , m_bHalt(false)
{
}

//!< 析构函数
iCAX::Tracker::CRepositoryUndoRedo::~CRepositoryUndoRedo()
{
}

//!< 获取仓储
std::weak_ptr<iCAX::Database::IRepository> iCAX::Tracker::CRepositoryUndoRedo::GetRepository() const
{
    return m_pRepository;
}

//!< 开始记录撤销还原
bool iCAX::Tracker::CRepositoryUndoRedo::BeginStep(IN const iCAX::Data::uuid& DomainID_, IN const std::string& strName_)
{
    if (m_bHalt || IsRecording())
    {
        return false;
    }

    auto _pRepository = m_pRepository.lock();
    if (!_pRepository)
    {
        return false;
    }

    try
    {
        m_pCurrentUndoScope = _pRepository->BeginUndoCommand(DomainID_, strName_);
        return m_pCurrentUndoScope != nullptr;
    }
    catch (...)
    {
        return false;
    }
}

//!< 停止撤销还原
bool iCAX::Tracker::CRepositoryUndoRedo::EndStep()
{
    if (!m_pCurrentUndoScope)
    {
        return false;
    }

    try
    {
        m_pCurrentUndoScope->End();
        m_pCurrentUndoScope.reset();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

//!< 是否正在记录
bool iCAX::Tracker::CRepositoryUndoRedo::IsRecording() const
{
    auto _pRepository = m_pRepository.lock();
    return _pRepository ? _pRepository->IsUndoCommandRecording() : false;
}

//!< 获取正在录制撤销还原的域
iCAX::Data::uuid iCAX::Tracker::CRepositoryUndoRedo::GetCurrentRecordingDomain() const
{
    auto _pRepository = m_pRepository.lock();
    return _pRepository ? _pRepository->GetCurrentUndoCommandDomain() : iCAX::Data::GenerateNilUUID();
}

//!< 是否可以重做
bool iCAX::Tracker::CRepositoryUndoRedo::CanRedo(IN const iCAX::Data::uuid& DomainID_) const
{
    auto _pRepository = m_pRepository.lock();
    return _pRepository ? _pRepository->CanRedo(DomainID_) : false;
}

//!< 是否可以撤销
bool iCAX::Tracker::CRepositoryUndoRedo::CanUndo(IN const iCAX::Data::uuid& DomainID_) const
{
    auto _pRepository = m_pRepository.lock();
    return _pRepository ? _pRepository->CanUndo(DomainID_) : false;
}

//!<  获取撤销步骤列表
std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Tracker::CRepositoryUndoRedo::GetUndoArray(IN const iCAX::Data::uuid& DomainID_) const
{
    auto _pRepository = m_pRepository.lock();
    return _pRepository ? _pRepository->GetUndoArray(DomainID_) : std::vector<std::tuple<iCAX::Data::uuid, std::string>>{};
}

//!< 获取重做步骤列表
std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Tracker::CRepositoryUndoRedo::GetRedoArray(IN const iCAX::Data::uuid& DomainID_) const
{
    auto _pRepository = m_pRepository.lock();
    return _pRepository ? _pRepository->GetRedoArray(DomainID_) : std::vector<std::tuple<iCAX::Data::uuid, std::string>>{};
}

//!< 重做
bool iCAX::Tracker::CRepositoryUndoRedo::Redo(IN const iCAX::Data::uuid& DomainID_)
{
    auto _pRepository = m_pRepository.lock();
    return _pRepository ? _pRepository->Redo(DomainID_) : false;
}

//!< 撤销
bool iCAX::Tracker::CRepositoryUndoRedo::Undo(IN const iCAX::Data::uuid& DomainID_)
{
    auto _pRepository = m_pRepository.lock();
    return _pRepository ? _pRepository->Undo(DomainID_) : false;
}

//!< 挂起
void iCAX::Tracker::CRepositoryUndoRedo::Halt(IN bool bHalt_)
{
    m_bHalt = bHalt_;
}

//!< 批量记录
void iCAX::Tracker::CRepositoryUndoRedo::BatchRecord(IN const std::list<iCAX::Database::OperationUnit>& Operations_)
{
    (void)Operations_;
}

//!< 修改前事件
void iCAX::Tracker::CRepositoryUndoRedo::OnRepositoryChanging(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    (void)pSender_;
    (void)Args_;
}

//!< 更改后事件
void iCAX::Tracker::CRepositoryUndoRedo::OnRepositoryChanged(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    (void)pSender_;
    (void)Args_;
}
