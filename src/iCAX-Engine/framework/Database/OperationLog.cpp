#include "pch.h"
#include "OperationLog.h"
#include "ComponentBase.h"
#include "IMetaRegistry.h"
#include "Data/VariantSerializer.h"


namespace
{
    using namespace iCAX::Data;
    using namespace iCAX::Database;

    constexpr const char* S_OperationLogHeaderRecord = "iCAX.OperationLog.Header";

    void ValidateOperationLogIdentity(IN const std::string& strMagic_, IN const uint32_t nVersion_)
    {
        if (strMagic_.empty())
        {
            throw std::invalid_argument("Operation journal magic cannot be empty");
        }
        if (nVersion_ == 0)
        {
            throw std::invalid_argument("Operation journal version cannot be zero");
        }
    }

    std::string ToString(IN const EOperationBatchKind Kind_)
    {
        switch (Kind_)
        {
        case EOperationBatchKind::LoadBaseline:
            return "LoadBaseline";
        case EOperationBatchKind::Transaction:
            return "Transaction";
        case EOperationBatchKind::Replay:
            return "Replay";
        case EOperationBatchKind::UserCommand:
            return "UserCommand";
        }

        throw std::runtime_error("Invalid operation batch kind");
    }

    EOperationBatchKind ToBatchKind(IN const std::string& strKind_)
    {
        if (strKind_ == "LoadBaseline")
        {
            return EOperationBatchKind::LoadBaseline;
        }
        if (strKind_ == "Transaction")
        {
            return EOperationBatchKind::Transaction;
        }
        if (strKind_ == "Replay")
        {
            return EOperationBatchKind::Replay;
        }
        if (strKind_ == "UserCommand")
        {
            return EOperationBatchKind::UserCommand;
        }

        throw std::runtime_error("Invalid operation batch kind: " + strKind_);
    }

    std::string ToString(IN const RepositoryEventArgs::EventType Type_)
    {
        switch (Type_)
        {
        case RepositoryEventArgs::kAddEntity:
            return "AddEntity";
        case RepositoryEventArgs::kDeleteEntity:
            return "DeleteEntity";
        case RepositoryEventArgs::kAddComponent:
            return "AddComponent";
        case RepositoryEventArgs::kRemoveComponent:
            return "RemoveComponent";
        case RepositoryEventArgs::kModifyComponent:
            return "ModifyComponent";
        case RepositoryEventArgs::kEnableComponent:
            return "EnableComponent";
        case RepositoryEventArgs::kDisableComponent:
            return "DisableComponent";
        }

        throw std::runtime_error("Invalid repository operation type");
    }

    RepositoryEventArgs::EventType ToOperationType(IN const std::string& strType_)
    {
        if (strType_ == "AddEntity")
        {
            return RepositoryEventArgs::kAddEntity;
        }
        if (strType_ == "DeleteEntity")
        {
            return RepositoryEventArgs::kDeleteEntity;
        }
        if (strType_ == "AddComponent")
        {
            return RepositoryEventArgs::kAddComponent;
        }
        if (strType_ == "RemoveComponent")
        {
            return RepositoryEventArgs::kRemoveComponent;
        }
        if (strType_ == "ModifyComponent")
        {
            return RepositoryEventArgs::kModifyComponent;
        }
        if (strType_ == "EnableComponent")
        {
            return RepositoryEventArgs::kEnableComponent;
        }
        if (strType_ == "DisableComponent")
        {
            return RepositoryEventArgs::kDisableComponent;
        }

        throw std::runtime_error("Invalid repository operation type: " + strType_);
    }

    const Variant& Require(IN const ObjectMap& Map_, IN const std::string& strName_)
    {
        auto _Ite = Map_.find(strName_);
        if (_Ite == Map_.end())
        {
            throw std::runtime_error("Missing OperationBatch field: " + strName_);
        }
        return _Ite->second;
    }

    ObjectMap AsObject(IN const Variant& Value_)
    {
        return Value_.To<ObjectMap>();
    }

    VariantArray AsArray(IN const Variant& Value_)
    {
        return Value_.To<VariantArray>();
    }

    PropertySet ReadProperties(IN const ObjectMap& Map_, IN const std::string& strName_)
    {
        auto _Ite = Map_.find(strName_);
        if (_Ite == Map_.end())
        {
            return {};
        }
        return _Ite->second.To<ObjectMap>();
    }

    std::optional<bool> ReadOptionalBool(IN const ObjectMap& Map_, IN const std::string& strName_)
    {
        auto _Ite = Map_.find(strName_);
        if (_Ite == Map_.end())
        {
            return std::nullopt;
        }
        return _Ite->second.To<bool>();
    }

    bool IsBlank(IN const std::string& Text_)
    {
        for (const auto _Char : Text_)
        {
            if (!std::isspace(static_cast<unsigned char>(_Char)))
            {
                return false;
            }
        }
        return true;
    }

    bool ReadNextNonBlankLine(IN std::istream& Input_, OUT std::string& strLine_)
    {
        while (std::getline(Input_, strLine_))
        {
            if (!IsBlank(strLine_))
            {
                return true;
            }
        }
        return false;
    }

    Variant OperationLogHeaderToVariant(IN const std::string& strMagic_, IN const uint32_t nVersion_)
    {
        ObjectMap _Header;
        _Header["record"] = Variant(std::string(S_OperationLogHeaderRecord));
        _Header["magic"] = Variant(strMagic_);
        _Header["version"] = Variant(static_cast<unsigned int>(nVersion_));
        return Variant(_Header);
    }

    void ValidateOperationLogHeader(IN const std::string& strHeaderLine_, IN const std::string& strMagic_, IN const uint32_t nVersion_)
    {
        auto _Object = iCAX::Data::VariantSerializer::Deserialize(strHeaderLine_).To<ObjectMap>();
        const auto _Record = Require(_Object, "record").To<std::string>();
        const auto _Magic = Require(_Object, "magic").To<std::string>();
        const auto _Version = Require(_Object, "version").To<unsigned int>();

        if (_Record != S_OperationLogHeaderRecord)
        {
            throw std::runtime_error("Invalid operation journal header record: " + _Record);
        }
        if (_Magic != strMagic_)
        {
            throw std::runtime_error("Operation journal magic mismatch: " + _Magic);
        }
        if (_Version != nVersion_)
        {
            throw std::runtime_error("Operation journal version mismatch: " + std::to_string(_Version));
        }
    }

    std::string SerializeOperationLogHeader(IN const std::string& strMagic_, IN const uint32_t nVersion_)
    {
        return iCAX::Data::VariantSerializer::Serialize(OperationLogHeaderToVariant(strMagic_, nVersion_));
    }

    bool ShouldKeepProperty(IN const IMetaRegistry& Meta_, IN const std::string& strComponentClass_, IN const std::string& strPropertyName_, IN const bool bRequirePersistent_)
    {
        if (!Meta_.HasTypeByName(strComponentClass_))
        {
            throw std::runtime_error("Operation log references unregistered component type: " + strComponentClass_);
        }
        if (!Meta_.HasPropertyByName(strComponentClass_, strPropertyName_))
        {
            throw std::runtime_error("Operation log references unregistered component property: " + strComponentClass_ + "." + strPropertyName_);
        }
        if (Meta_.GetPropertyKindByName(strComponentClass_, strPropertyName_) != EPropertyKind::Value)
        {
            return false;
        }
        if (Meta_.GetPropertyChangePolicyByName(strComponentClass_, strPropertyName_) != EPropertyChangePolicy::Transactional)
        {
            return false;
        }
        if (bRequirePersistent_ && Meta_.GetPropertyPersistenceByName(strComponentClass_, strPropertyName_) != EPropertyPersistence::Persistent)
        {
            return false;
        }
        return true;
    }

    PropertySet FilterProperties(IN const IMetaRegistry& Meta_, IN const std::string& strComponentClass_, IN const PropertySet& Properties_, IN const bool bRequirePersistent_)
    {
        PropertySet _Result;
        for (const auto& [_strName, _Value] : Properties_)
        {
            if (ShouldKeepProperty(Meta_, strComponentClass_, _strName, bRequirePersistent_))
            {
                _Result[_strName] = _Value;
            }
        }
        return _Result;
    }

    CRepositoryOperation FilterOperation(IN const CRepositoryOperation& Operation_, IN const IMetaRegistry& Meta_, IN const bool bRequirePersistent_)
    {
        CRepositoryOperation _Result = Operation_;

        // 结构性操作必须保留；字段集合只按 meta 裁剪。
        // Modify 操作要求 Previous/New 成对出现，避免只保留一侧导致回放无法恢复。
        switch (_Result.Type)
        {
        case RepositoryEventArgs::kAddComponent:
        case RepositoryEventArgs::kRemoveComponent:
            _Result.PreviousProperties = FilterProperties(Meta_, _Result.ComponentClass, _Result.PreviousProperties, bRequirePersistent_);
            _Result.NewProperties = FilterProperties(Meta_, _Result.ComponentClass, _Result.NewProperties, bRequirePersistent_);
            return _Result;
        case RepositoryEventArgs::kModifyComponent:
            _Result.PreviousProperties = FilterProperties(Meta_, _Result.ComponentClass, _Result.PreviousProperties, bRequirePersistent_);
            _Result.NewProperties = FilterProperties(Meta_, _Result.ComponentClass, _Result.NewProperties, bRequirePersistent_);
            for (auto _Ite = _Result.PreviousProperties.begin(); _Ite != _Result.PreviousProperties.end(); )
            {
                if (!_Result.NewProperties.contains(_Ite->first))
                {
                    _Ite = _Result.PreviousProperties.erase(_Ite);
                }
                else
                {
                    ++_Ite;
                }
            }
            for (auto _Ite = _Result.NewProperties.begin(); _Ite != _Result.NewProperties.end(); )
            {
                if (!_Result.PreviousProperties.contains(_Ite->first))
                {
                    _Ite = _Result.NewProperties.erase(_Ite);
                }
                else
                {
                    ++_Ite;
                }
            }
            return _Result;
        case RepositoryEventArgs::kEnableComponent:
        case RepositoryEventArgs::kDisableComponent:
            return _Result;
        case RepositoryEventArgs::kAddEntity:
        case RepositoryEventArgs::kDeleteEntity:
            return _Result;
        }

        throw std::runtime_error("Invalid repository operation type");
    }

    COperationBatch FilterBatch(IN const COperationBatch& Batch_, IN const IMetaRegistry& Meta_, IN const bool bRequirePersistent_)
    {
        COperationBatch _Result;
        _Result.Kind = Batch_.Kind;
        _Result.Name = Batch_.Name;

        // 过滤时只删除不可参与目标语义的字段或空操作，不改变剩余操作的相对顺序。
        for (const auto& _Operation : Batch_.Operations)
        {
            auto _Filtered = FilterOperation(_Operation, Meta_, bRequirePersistent_);
            if (!_Filtered.IsEmpty())
            {
                _Result.Operations.push_back(std::move(_Filtered));
            }
        }
        return _Result;
    }

    ObjectMap ToVariantObject(IN const CRepositoryOperation& Operation_)
    {
        ObjectMap _Object = {
            { "type", Variant(ToString(Operation_.Type)) },
            { "entityID", Variant(Operation_.EntityID) },
            { "componentClass", Variant(Operation_.ComponentClass) },
            { "previousProperties", Variant(Operation_.PreviousProperties) },
            { "newProperties", Variant(Operation_.NewProperties) }
        };
        if (Operation_.PreviousEnabled.has_value())
        {
            _Object["previousEnabled"] = Variant(*Operation_.PreviousEnabled);
        }
        if (Operation_.NewEnabled.has_value())
        {
            _Object["newEnabled"] = Variant(*Operation_.NewEnabled);
        }
        return _Object;
    }

    CRepositoryOperation ToRepositoryOperation(IN const Variant& Value_)
    {
        auto _Object = AsObject(Value_);

        CRepositoryOperation _Result;
        _Result.Type = ToOperationType(Require(_Object, "type").To<std::string>());
        _Result.EntityID = Require(_Object, "entityID").To<uuid>();
        _Result.ComponentClass = Require(_Object, "componentClass").To<std::string>();
        _Result.PreviousProperties = ReadProperties(_Object, "previousProperties");
        _Result.NewProperties = ReadProperties(_Object, "newProperties");
        _Result.PreviousEnabled = ReadOptionalBool(_Object, "previousEnabled");
        _Result.NewEnabled = ReadOptionalBool(_Object, "newEnabled");
        return _Result;
    }
}

bool iCAX::Database::CRepositoryOperation::IsEmpty() const
{
    switch (Type)
    {
    case RepositoryEventArgs::kAddEntity:
    case RepositoryEventArgs::kDeleteEntity:
    case RepositoryEventArgs::kAddComponent:
    case RepositoryEventArgs::kRemoveComponent:
        return false;
    case RepositoryEventArgs::kModifyComponent:
        return PreviousProperties.empty() || NewProperties.empty();
    case RepositoryEventArgs::kEnableComponent:
    case RepositoryEventArgs::kDisableComponent:
        return false;
    }

    throw std::runtime_error("Invalid repository operation type");
}

bool iCAX::Database::COperationBatch::IsEmpty() const
{
    return Operations.empty();
}

iCAX::Database::COperationBatchBuilder::COperationBatchBuilder(IN EOperationBatchKind Kind_, IN const std::string& strName_)
{
    m_Batch.Kind = Kind_;
    m_Batch.Name = strName_;
}

void iCAX::Database::COperationBatchBuilder::RecordRepositoryEvent(IN const RepositoryEventArgs& Args_)
{
    if (m_Batch.Kind == EOperationBatchKind::LoadBaseline)
    {
        return;
    }

    AppendBatch(MakeOperationBatchFromRepositoryEvent(Args_, m_Batch.Kind, m_Batch.Name));
}

void iCAX::Database::COperationBatchBuilder::AppendOperation(IN const CRepositoryOperation& Operation_)
{
    if (!Operation_.IsEmpty())
    {
        m_Batch.Operations.push_back(Operation_);
    }
}

void iCAX::Database::COperationBatchBuilder::AppendBatch(IN const COperationBatch& Batch_)
{
    for (const auto& _Operation : Batch_.Operations)
    {
        AppendOperation(_Operation);
    }
}

iCAX::Database::COperationBatch iCAX::Database::COperationBatchBuilder::Build() const
{
    return m_Batch;
}

iCAX::Database::COperationBatch iCAX::Database::MakeOperationBatchFromRepositoryEvent(IN const RepositoryEventArgs& Args_, IN EOperationBatchKind Kind_, IN const std::string& strName_)
{
    COperationBatch _Result;
    _Result.Kind = Kind_;
    _Result.Name = strName_;

    switch (Args_.nType)
    {
    case RepositoryEventArgs::kAddEntity:
    case RepositoryEventArgs::kDeleteEntity:
    case RepositoryEventArgs::kAddComponent:
    case RepositoryEventArgs::kRemoveComponent:
    case RepositoryEventArgs::kModifyComponent:
    case RepositoryEventArgs::kEnableComponent:
    case RepositoryEventArgs::kDisableComponent:
        break;
    case RepositoryEventArgs::kBatchChanged:
        return _Result;
    default:
        throw std::runtime_error("Invalid repository operation type");
    }

    CRepositoryOperation _Operation;
    _Operation.Type = Args_.nType;
    _Operation.EntityID = Args_.EntityID;
    _Operation.ComponentClass = Args_.strClassName;
    _Operation.PreviousProperties = Args_.PreviousProperties;
    _Operation.NewProperties = Args_.NewProperties;
    if (Args_.nType == RepositoryEventArgs::kAddComponent)
    {
        _Operation.NewEnabled = Args_.pComponent ? Args_.pComponent->IsEnable() : true;
    }
    else if (Args_.nType == RepositoryEventArgs::kRemoveComponent)
    {
        _Operation.PreviousEnabled = Args_.pComponent ? Args_.pComponent->IsEnable() : true;
    }
    else if (Args_.nType == RepositoryEventArgs::kEnableComponent)
    {
        _Operation.PreviousEnabled = false;
        _Operation.NewEnabled = true;
    }
    else if (Args_.nType == RepositoryEventArgs::kDisableComponent)
    {
        _Operation.PreviousEnabled = true;
        _Operation.NewEnabled = false;
    }

    if (!_Operation.IsEmpty())
    {
        _Result.Operations.push_back(std::move(_Operation));
    }
    return _Result;
}

iCAX::Database::CChangeSet iCAX::Database::BuildChangeSetFromOperationBatch(IN const COperationBatch& Batch_)
{
    CChangeSetBuilder _Builder(Batch_.Kind, Batch_.Name);

    // ChangeSet 是外部通知和版本/派生字段失效使用的净摘要。
    // 这里允许合并和抵消变更，但结果不能再用于回放，因为字段真实顺序会丢失。
    for (const auto& _Operation : Batch_.Operations)
    {
        switch (_Operation.Type)
        {
        case RepositoryEventArgs::kAddEntity:
            _Builder.RecordAddEntity({ _Operation.EntityID });
            break;
        case RepositoryEventArgs::kDeleteEntity:
            _Builder.RecordDeleteEntity({ _Operation.EntityID });
            break;
        case RepositoryEventArgs::kAddComponent:
            _Builder.RecordAddComponent({ _Operation.EntityID, _Operation.ComponentClass }, _Operation.NewProperties);
            break;
        case RepositoryEventArgs::kRemoveComponent:
            _Builder.RecordRemoveComponent({ _Operation.EntityID, _Operation.ComponentClass }, _Operation.PreviousProperties);
            break;
        case RepositoryEventArgs::kModifyComponent:
            _Builder.RecordModifyComponent({ _Operation.EntityID, _Operation.ComponentClass }, _Operation.PreviousProperties, _Operation.NewProperties);
            break;
        case RepositoryEventArgs::kEnableComponent:
        case RepositoryEventArgs::kDisableComponent:
            _Builder.RecordComponentEnabledState(
                { _Operation.EntityID, _Operation.ComponentClass },
                _Operation.PreviousEnabled.value_or(_Operation.Type == RepositoryEventArgs::kDisableComponent),
                _Operation.NewEnabled.value_or(_Operation.Type == RepositoryEventArgs::kEnableComponent));
            break;
        default:
            throw std::runtime_error("Invalid repository operation type");
        }
    }
    return _Builder.Build();
}

iCAX::Database::COperationBatch iCAX::Database::FilterTransactionalOperationBatch(IN const COperationBatch& Batch_, IN const IMetaRegistry& Meta_)
{
    return FilterBatch(Batch_, Meta_, false);
}

iCAX::Database::COperationBatch iCAX::Database::FilterPersistentOperationBatch(IN const COperationBatch& Batch_, IN const IMetaRegistry& Meta_)
{
    return FilterBatch(Batch_, Meta_, true);
}

iCAX::Database::COperationBatch iCAX::Database::MakeInverseOperationBatch(IN const COperationBatch& Batch_, IN const std::string& strName_)
{
    COperationBatch _Result;
    _Result.Kind = EOperationBatchKind::Replay;
    if (!strName_.empty())
    {
        _Result.Name = strName_;
    }
    else if (!Batch_.Name.empty())
    {
        _Result.Name = "Undo " + Batch_.Name;
    }
    else
    {
        _Result.Name = "Undo";
    }

    // 反向批次必须倒序生成。比如先改 Width 再改 Length，撤销时必须先恢复 Length 再恢复 Width，
    // 否则中间状态可能违反组件自身约束。
    for (auto _Ite = Batch_.Operations.rbegin(); _Ite != Batch_.Operations.rend(); ++_Ite)
    {
        CRepositoryOperation _Inverse;
        _Inverse.EntityID = _Ite->EntityID;
        _Inverse.ComponentClass = _Ite->ComponentClass;

        switch (_Ite->Type)
        {
        case RepositoryEventArgs::kAddEntity:
            _Inverse.Type = RepositoryEventArgs::kDeleteEntity;
            break;
        case RepositoryEventArgs::kDeleteEntity:
            _Inverse.Type = RepositoryEventArgs::kAddEntity;
            break;
        case RepositoryEventArgs::kAddComponent:
            _Inverse.Type = RepositoryEventArgs::kRemoveComponent;
            _Inverse.PreviousProperties = _Ite->NewProperties;
            _Inverse.PreviousEnabled = _Ite->NewEnabled;
            break;
        case RepositoryEventArgs::kRemoveComponent:
            _Inverse.Type = RepositoryEventArgs::kAddComponent;
            _Inverse.NewProperties = _Ite->PreviousProperties;
            _Inverse.NewEnabled = _Ite->PreviousEnabled;
            break;
        case RepositoryEventArgs::kModifyComponent:
            _Inverse.Type = RepositoryEventArgs::kModifyComponent;
            _Inverse.PreviousProperties = _Ite->NewProperties;
            _Inverse.NewProperties = _Ite->PreviousProperties;
            break;
        case RepositoryEventArgs::kEnableComponent:
            _Inverse.Type = RepositoryEventArgs::kDisableComponent;
            _Inverse.PreviousEnabled = _Ite->NewEnabled;
            _Inverse.NewEnabled = _Ite->PreviousEnabled;
            break;
        case RepositoryEventArgs::kDisableComponent:
            _Inverse.Type = RepositoryEventArgs::kEnableComponent;
            _Inverse.PreviousEnabled = _Ite->NewEnabled;
            _Inverse.NewEnabled = _Ite->PreviousEnabled;
            break;
        default:
            throw std::runtime_error("Invalid repository operation type");
        }

        if (!_Inverse.IsEmpty())
        {
            _Result.Operations.push_back(std::move(_Inverse));
        }
    }
    return _Result;
}

iCAX::Data::Variant iCAX::Database::OperationBatchToVariant(IN const COperationBatch& Batch_)
{
    VariantArray _Operations;
    for (const auto& _Operation : Batch_.Operations)
    {
        _Operations.emplace_back(ToVariantObject(_Operation));
    }

    ObjectMap _Object;
    _Object["kind"] = Variant(ToString(Batch_.Kind));
    _Object["name"] = Variant(Batch_.Name);
    _Object["operations"] = Variant(_Operations);
    return Variant(_Object);
}

iCAX::Database::COperationBatch iCAX::Database::OperationBatchFromVariant(IN const iCAX::Data::Variant& Value_)
{
    auto _Object = Value_.To<ObjectMap>();

    COperationBatch _Result;
    _Result.Kind = ToBatchKind(Require(_Object, "kind").To<std::string>());
    _Result.Name = Require(_Object, "name").To<std::string>();

    auto _Ite = _Object.find("operations");
    if (_Ite == _Object.end())
    {
        return _Result;
    }

    for (const auto& _Item : AsArray(_Ite->second))
    {
        auto _Operation = ToRepositoryOperation(_Item);
        if (!_Operation.IsEmpty())
        {
            _Result.Operations.push_back(std::move(_Operation));
        }
    }
    return _Result;
}

std::string iCAX::Database::SerializeOperationBatch(IN const COperationBatch& Batch_)
{
    return iCAX::Data::VariantSerializer::Serialize(OperationBatchToVariant(Batch_));
}

iCAX::Database::COperationBatch iCAX::Database::DeserializeOperationBatch(IN const std::string& Text_)
{
    return OperationBatchFromVariant(iCAX::Data::VariantSerializer::Deserialize(Text_));
}

iCAX::Database::COperationBatchJournal::~COperationBatchJournal()
{
    Close();
}

void iCAX::Database::COperationBatchJournal::Open(
    IN const std::string& strPath_,
    IN const bool bTruncate_,
    IN const std::string& strMagic_,
    IN const uint32_t nVersion_)
{
    ValidateOperationLogIdentity(strMagic_, nVersion_);
    Close();

    bool _bNeedWriteHeader = true;
    if (!bTruncate_)
    {
        std::ifstream _Input(strPath_, std::ios::in | std::ios::binary);
        if (_Input.is_open())
        {
            std::string _HeaderLine;
            if (ReadNextNonBlankLine(_Input, _HeaderLine))
            {
                ValidateOperationLogHeader(_HeaderLine, strMagic_, nVersion_);
                _bNeedWriteHeader = false;
            }
        }
    }

    auto _Mode = std::ios::out | std::ios::binary;
    _Mode |= bTruncate_ ? std::ios::trunc : std::ios::app;
    m_Stream.open(strPath_, _Mode);
    if (!m_Stream.is_open())
    {
        throw std::runtime_error("Failed to open operation journal: " + strPath_);
    }
    m_strPath = strPath_;

    if (_bNeedWriteHeader)
    {
        m_Stream << SerializeOperationLogHeader(strMagic_, nVersion_) << '\n';
        m_Stream.flush();
        if (!m_Stream.good())
        {
            throw std::runtime_error("Failed to write operation journal header: " + m_strPath);
        }
    }
}

void iCAX::Database::COperationBatchJournal::Close()
{
    if (m_Stream.is_open())
    {
        m_Stream.flush();
        m_Stream.close();
    }
    m_strPath.clear();
}

bool iCAX::Database::COperationBatchJournal::IsOpen() const
{
    return m_Stream.is_open();
}

const std::string& iCAX::Database::COperationBatchJournal::GetPath() const
{
    return m_strPath;
}

void iCAX::Database::COperationBatchJournal::Append(IN const COperationBatch& Batch_)
{
    if (!m_Stream.is_open() || Batch_.IsEmpty())
    {
        return;
    }

    m_Stream << SerializeOperationBatch(Batch_) << '\n';
    m_Stream.flush();
    if (!m_Stream.good())
    {
        throw std::runtime_error("Failed to append operation journal: " + m_strPath);
    }
}

std::vector<iCAX::Database::COperationBatch> iCAX::Database::COperationBatchJournal::ReadAll(
    IN const std::string& strPath_,
    IN const std::string& strMagic_,
    IN const uint32_t nVersion_) const
{
    ValidateOperationLogIdentity(strMagic_, nVersion_);

    std::ifstream _Input(strPath_, std::ios::in | std::ios::binary);
    if (!_Input.is_open())
    {
        return {};
    }

    std::vector<COperationBatch> _Result;
    std::string _Line;
    if (!ReadNextNonBlankLine(_Input, _Line))
    {
        return _Result;
    }

    ValidateOperationLogHeader(_Line, strMagic_, nVersion_);

    while (std::getline(_Input, _Line))
    {
        if (IsBlank(_Line))
        {
            continue;
        }

        try
        {
            auto _Batch = DeserializeOperationBatch(_Line);
            if (!_Batch.IsEmpty())
            {
                _Result.push_back(std::move(_Batch));
            }
        }
        catch (...)
        {
            // 追加式日志可能在崩溃时留下半行。遇到无法解析的行即停止，
            // 保留此前所有完整批次，避免把损坏尾部当作有效操作。
            break;
        }
    }
    return _Result;
}
