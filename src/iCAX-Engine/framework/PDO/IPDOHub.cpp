#include "pch.h"
#include "IPDOHub.h"
#include "PDOHub.h"

//! 生成PDO集线器
std::shared_ptr<iCAX::PDO::IPDOHub> iCAX::PDO::GeneratePDOHub(IN std::vector<PDODecl> Descs_)
{
    auto _pHub = std::make_shared<CPDOHub>();
    _pHub->Intialize(Descs_);
    return _pHub;
}

std::shared_ptr<iCAX::PDO::IPDOHub> iCAX::PDO::GeneratePDOHub(IN const CPDOHubCreateInfo& CreateInfo_)
{
    auto _pHub = std::make_shared<CPDOHub>();
    _pHub->Intialize(CreateInfo_);
    return _pHub;
}
