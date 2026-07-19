#include "pch.h"
#include "IRepository.h"
#include "Repository.h"


std::shared_ptr<iCAX::Database::IRepository> iCAX::Database::GenerateRepository(
    IN const iCAX::Data::uuid& RepositoryID_,
    IN std::shared_ptr<IMetaRegistry> pMetaRegistry_)
{
    auto _pRepository = std::make_shared<CRepository>(RepositoryID_, std::move(pMetaRegistry_));
    _pRepository->Initialzie();
    return _pRepository;
}
