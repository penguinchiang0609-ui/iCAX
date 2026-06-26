#include "pch.h"
#include "PDODecl.h"
#include "Data/StableId.h"

//! 生成PDO ID
iCAX::PDO::PDOID iCAX::PDO::MakePDOID(IN const std::string& strTypeName_, IN const std::string& strInstanceName_)
{
    return iCAX::Data::MakeStableId(strTypeName_, strInstanceName_);
}
