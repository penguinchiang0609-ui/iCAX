#include "pch.h"
#include "InputPDODecl.h"


const char* iCAX::InputPDO::GetInputPDOPayloadTypeName(IN EInputPDOPayloadKind eKind_)
{
    switch (eKind_)
    {
    case EInputPDOPayloadKind::State:
        return "input.state";
    default:
        return "";
    }
}

iCAX::PDO::PDODecl iCAX::InputPDO::MakeInputPDODecl(
    IN EInputPDOPayloadKind eKind_,
    IN const std::string& strInstanceName_,
    IN iCAX::PDO::PDODirection eDirection_,
    IN uint64_t nPayloadCapacity_)
{
    const char* _pTypeName = GetInputPDOPayloadTypeName(eKind_);
    if (_pTypeName == nullptr || _pTypeName[0] == '\0')
    {
        throw std::invalid_argument("Input PDO payload kind is invalid");
    }
    if (strInstanceName_.empty())
    {
        throw std::invalid_argument("Input PDO instance name cannot be empty");
    }
    if (eDirection_ != iCAX::PDO::kDirection2Inner)
    {
        throw std::invalid_argument("Input PDO direction must be kDirection2Inner");
    }
    if (nPayloadCapacity_ < sizeof(SInputStatePDOHeader)
        || nPayloadCapacity_ > static_cast<uint64_t>((std::numeric_limits<int>::max)()))
    {
        throw std::invalid_argument("Input PDO payload capacity is invalid");
    }

    return iCAX::PDO::PDODecl{
        kInputPDOLayoutVersion,
        iCAX::PDO::MakePDOID(_pTypeName, strInstanceName_),
        eDirection_,
        static_cast<int>(nPayloadCapacity_)
    };
}

iCAX::PDO::PDODecl iCAX::InputPDO::MakeInputStatePDODecl(IN const std::string& strInstanceName_)
{
    return MakeInputPDODecl(
        EInputPDOPayloadKind::State,
        strInstanceName_,
        iCAX::PDO::kDirection2Inner,
        sizeof(SInputStatePDOHeader));
}
