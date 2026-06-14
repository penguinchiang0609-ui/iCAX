#include "pch.h"
#include "IRepository.h"
#include "Repository.h"

//!< 生成数据仓储
std::shared_ptr<iCAX::Database::IRepository> iCAX::Database::GenerateRepository(IN const iCAX::Data::uuid& RepositoryID_)
{
    auto _pRepository = std::make_shared<CRepository>(RepositoryID_);
    _pRepository->Initialzie();
    return _pRepository;
}
