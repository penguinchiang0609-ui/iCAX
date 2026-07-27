#include "pch.h"

#include "EntityQuery.h"

#include "IEntity.h"
#include "IMetaRegistry.h"
#include "Repository.h"

#include <cmath>
#include <type_traits>

namespace
{
    using namespace iCAX::Data;
    using namespace iCAX::Database;

    struct SNumericValue final
    {
        enum class EKind
        {
            SignedInteger,
            UnsignedInteger,
            FloatingPoint,
        } Kind = EKind::SignedInteger;

        long long Signed = 0;
        unsigned long long Unsigned = 0;
        long double Floating = 0;
    };

    struct SSourceRow final
    {
        uuid EntityID;
        std::shared_ptr<IEntity> pEntity;
    };

    struct SGroup final
    {
        PropertyArray Key;
        std::vector<SSourceRow> Members;
    };

    struct SMaterializedRow final
    {
        PropertyArray Values;
        std::vector<SSourceRow> Members;
    };

    std::string Upper(IN std::string Text_)
    {
        std::transform(
            Text_.begin(),
            Text_.end(),
            Text_.begin(),
            [](IN const unsigned char Character_)
            {
                return static_cast<char>(std::toupper(Character_));
            });
        return Text_;
    }

    std::string TopLevelPropertyName(IN const std::string& strPropertyPath_)
    {
        const auto _nSeparator = strPropertyPath_.find_first_of(".[");
        return strPropertyPath_.substr(0, _nSeparator);
    }

    bool IsNil(IN const PropertyValue& Value_)
    {
        return Value_.Is<std::monostate>();
    }

    std::optional<SNumericValue> ToNumber(IN const PropertyValue& Value_)
    {
        return std::visit([](IN const auto& Value_) -> std::optional<SNumericValue>
        {
            using TValue = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_floating_point_v<TValue>)
            {
                SNumericValue _Result;
                _Result.Kind = SNumericValue::EKind::FloatingPoint;
                _Result.Floating = static_cast<long double>(Value_);
                return _Result;
            }
            else if constexpr (std::is_integral_v<TValue>
                && !std::is_same_v<TValue, bool>)
            {
                SNumericValue _Result;
                if constexpr (std::is_signed_v<TValue>)
                {
                    _Result.Kind = SNumericValue::EKind::SignedInteger;
                    _Result.Signed = static_cast<long long>(Value_);
                }
                else
                {
                    _Result.Kind = SNumericValue::EKind::UnsignedInteger;
                    _Result.Unsigned =
                        static_cast<unsigned long long>(Value_);
                }
                return _Result;
            }
            return std::nullopt;
        }, Value_.m_Value);
    }

    long double ToFloating(IN const SNumericValue& Value_)
    {
        switch (Value_.Kind)
        {
        case SNumericValue::EKind::SignedInteger:
            return static_cast<long double>(Value_.Signed);
        case SNumericValue::EKind::UnsignedInteger:
            return static_cast<long double>(Value_.Unsigned);
        case SNumericValue::EKind::FloatingPoint:
            return Value_.Floating;
        }
        return 0;
    }

    std::optional<int> CompareNumbers(
        IN const SNumericValue& Left_,
        IN const SNumericValue& Right_)
    {
        if (Left_.Kind == SNumericValue::EKind::FloatingPoint
            || Right_.Kind == SNumericValue::EKind::FloatingPoint)
        {
            const auto _Left = ToFloating(Left_);
            const auto _Right = ToFloating(Right_);
            if (std::isnan(_Left) || std::isnan(_Right))
            {
                return std::nullopt;
            }
            return _Left < _Right ? -1 : _Left > _Right ? 1 : 0;
        }

        if (Left_.Kind == SNumericValue::EKind::SignedInteger
            && Right_.Kind == SNumericValue::EKind::SignedInteger)
        {
            return Left_.Signed < Right_.Signed
                ? -1
                : Left_.Signed > Right_.Signed ? 1 : 0;
        }
        if (Left_.Kind == SNumericValue::EKind::UnsignedInteger
            && Right_.Kind == SNumericValue::EKind::UnsignedInteger)
        {
            return Left_.Unsigned < Right_.Unsigned
                ? -1
                : Left_.Unsigned > Right_.Unsigned ? 1 : 0;
        }
        if (Left_.Kind == SNumericValue::EKind::SignedInteger)
        {
            if (Left_.Signed < 0)
            {
                return -1;
            }
            const auto _Left = static_cast<unsigned long long>(Left_.Signed);
            return _Left < Right_.Unsigned
                ? -1
                : _Left > Right_.Unsigned ? 1 : 0;
        }
        if (Right_.Signed < 0)
        {
            return 1;
        }
        const auto _Right = static_cast<unsigned long long>(Right_.Signed);
        return Left_.Unsigned < _Right
            ? -1
            : Left_.Unsigned > _Right ? 1 : 0;
    }

    std::optional<int> CompareValues(
        IN const PropertyValue& Left_,
        IN const PropertyValue& Right_)
    {
        if (IsNil(Left_) || IsNil(Right_))
        {
            if (IsNil(Left_) && IsNil(Right_))
            {
                return 0;
            }
            return IsNil(Left_) ? 1 : -1;
        }

        const auto _LeftNumber = ToNumber(Left_);
        const auto _RightNumber = ToNumber(Right_);
        if (_LeftNumber && _RightNumber)
        {
            return CompareNumbers(*_LeftNumber, *_RightNumber);
        }
        if (Left_.m_Value.index() != Right_.m_Value.index())
        {
            return Left_.m_Value.index() < Right_.m_Value.index() ? -1 : 1;
        }
        try
        {
            return Left_ < Right_ ? -1 : Left_ > Right_ ? 1 : 0;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool ValuesEqual(
        IN const PropertyValue& Left_,
        IN const PropertyValue& Right_)
    {
        const auto _Order = CompareValues(Left_, Right_);
        return _Order && *_Order == 0;
    }

    bool KeysEqual(
        IN const PropertyArray& Left_,
        IN const PropertyArray& Right_)
    {
        if (Left_.size() != Right_.size())
        {
            return false;
        }
        for (size_t _Index = 0; _Index < Left_.size(); ++_Index)
        {
            if (!ValuesEqual(Left_[_Index], Right_[_Index]))
            {
                return false;
            }
        }
        return true;
    }

    std::uint64_t BindPageOperand(
        IN const std::optional<SEntityValueOperand>& Operand_,
        IN const ObjectMap& Parameters_,
        IN const std::string& strClause_)
    {
        if (!Operand_)
        {
            return 0;
        }

        const PropertyValue* _pValue = &Operand_->Literal;
        if (Operand_->Type == EEntityValueOperandType::Parameter)
        {
            const auto _Parameter =
                Parameters_.find(Operand_->ParameterName);
            if (_Parameter == Parameters_.end())
            {
                throw std::invalid_argument(
                    "Entity query " + strClause_
                    + " parameter is missing: "
                    + Operand_->ParameterName);
            }
            _pValue = &_Parameter->second;
        }

        return std::visit(
            [&strClause_](IN const auto& Value_) -> std::uint64_t
            {
                using TValue = std::decay_t<decltype(Value_)>;
                if constexpr (std::is_integral_v<TValue>
                    && !std::is_same_v<TValue, bool>)
                {
                    if constexpr (std::is_signed_v<TValue>)
                    {
                        if (Value_ < 0)
                        {
                            throw std::invalid_argument(
                                "Entity query " + strClause_
                                + " must be a non-negative integer");
                        }
                    }
                    return static_cast<std::uint64_t>(Value_);
                }
                else
                {
                    throw std::invalid_argument(
                        "Entity query " + strClause_
                        + " must be a non-negative integer");
                }
            },
            _pValue->m_Value);
    }

    bool CompareMemberEntityIDs(
        IN const SMaterializedRow& Left_,
        IN const SMaterializedRow& Right_)
    {
        const auto _nCommonSize =
            (std::min)(Left_.Members.size(), Right_.Members.size());
        for (size_t _Index = 0; _Index < _nCommonSize; ++_Index)
        {
            if (Left_.Members[_Index].EntityID
                == Right_.Members[_Index].EntityID)
            {
                continue;
            }
            return Left_.Members[_Index].EntityID
                < Right_.Members[_Index].EntityID;
        }
        return Left_.Members.size() < Right_.Members.size();
    }

    void ValidateField(
        IN const SEntityQueryField& Field_,
        IN const IMetaRegistry& Meta_)
    {
        if (Field_.Type == EEntityQueryFieldType::EntityID)
        {
            return;
        }
        if (!Meta_.HasTypeByName(Field_.ComponentClass))
        {
            throw std::invalid_argument(
                "Entity query references an unregistered component type: "
                + Field_.ComponentClass);
        }
        const auto _strPropertyName =
            TopLevelPropertyName(Field_.PropertyPath);
        if (_strPropertyName.empty()
            || !Meta_.HasPropertyByName(
                Field_.ComponentClass,
                _strPropertyName))
        {
            throw std::invalid_argument(
                "Entity query references an unregistered component property: "
                + Field_.ComponentClass + "." + Field_.PropertyPath);
        }
    }

    PropertyValue ReadField(
        IN const SSourceRow& Row_,
        IN const SEntityQueryField& Field_)
    {
        if (Field_.Type == EEntityQueryFieldType::EntityID)
        {
            return PropertyValue(Row_.EntityID);
        }
        if (!Row_.pEntity)
        {
            return PropertyValue::Nil;
        }
        const auto _pComponent =
            Row_.pEntity->GetComponent(Field_.ComponentClass);
        if (!_pComponent)
        {
            return PropertyValue::Nil;
        }

        const auto _strPropertyName =
            TopLevelPropertyName(Field_.PropertyPath);
        try
        {
            auto _Value = _pComponent->GetProperty(_strPropertyName);
            if (_strPropertyName.size() == Field_.PropertyPath.size())
            {
                return _Value;
            }
            auto _strNestedPath =
                Field_.PropertyPath.substr(_strPropertyName.size());
            if (!_strNestedPath.empty() && _strNestedPath.front() == '.')
            {
                _strNestedPath.erase(_strNestedPath.begin());
            }
            const auto _Nested = _Value.GetByPath(_strNestedPath);
            return _Nested ? *_Nested : PropertyValue::Nil;
        }
        catch (...)
        {
            return PropertyValue::Nil;
        }
    }

    bool ContainsField(
        IN const std::vector<SEntityQueryField>& Fields_,
        IN const SEntityQueryField& Field_)
    {
        return std::find(Fields_.begin(), Fields_.end(), Field_)
            != Fields_.end();
    }

    std::string DefaultProjectionName(
        IN const SEntityQueryProjection& Projection_)
    {
        if (Projection_.Field.Type == EEntityQueryFieldType::EntityID
            && Projection_.Aggregate == EEntityQueryAggregate::None)
        {
            return "ENTITYID";
        }

        const auto _strField =
            Projection_.Field.Type == EEntityQueryFieldType::EntityID
            ? std::string("ENTITYID")
            : Projection_.Field.ComponentClass
                + "." + Projection_.Field.PropertyPath;
        switch (Projection_.Aggregate)
        {
        case EEntityQueryAggregate::None:
            return _strField;
        case EEntityQueryAggregate::Count:
            return Projection_.bCountAll ? "COUNT(*)" : "COUNT(" + _strField + ")";
        case EEntityQueryAggregate::Sum:
            return "SUM(" + _strField + ")";
        case EEntityQueryAggregate::Average:
            return "AVG(" + _strField + ")";
        case EEntityQueryAggregate::Minimum:
            return "MIN(" + _strField + ")";
        case EEntityQueryAggregate::Maximum:
            return "MAX(" + _strField + ")";
        }
        return _strField;
    }

    std::vector<SEntityQueryProjection> NormalizeProjections(
        IN const std::vector<SEntityQueryProjection>& Projections_)
    {
        std::vector<SEntityQueryProjection> _Result;
        SEntityQueryProjection _EntityID;
        _EntityID.Field.Type = EEntityQueryFieldType::EntityID;
        _EntityID.Alias = "ENTITYID";
        _Result.push_back(std::move(_EntityID));

        for (auto _Projection : Projections_)
        {
            if (_Projection.Aggregate == EEntityQueryAggregate::None
                && _Projection.Field.Type == EEntityQueryFieldType::EntityID)
            {
                continue;
            }
            if (_Projection.Alias.empty())
            {
                _Projection.Alias = DefaultProjectionName(_Projection);
            }
            _Result.push_back(std::move(_Projection));
        }

        std::set<std::string> _Aliases;
        for (const auto& _Projection : _Result)
        {
            const auto _strAlias = Upper(_Projection.Alias);
            if (_strAlias.empty() || !_Aliases.insert(_strAlias).second)
            {
                throw std::invalid_argument(
                    "Entity query projection aliases must be unique");
            }
        }
        return _Result;
    }

    bool HasAggregate(
        IN const std::vector<SEntityQueryProjection>& Projections_)
    {
        return std::any_of(
            Projections_.begin(),
            Projections_.end(),
            [](IN const SEntityQueryProjection& Projection_)
            {
                return Projection_.Aggregate
                    != EEntityQueryAggregate::None;
            });
    }

    PropertyValue AggregateProjection(
        IN const SEntityQueryProjection& Projection_,
        IN const std::vector<SSourceRow>& Members_,
        IN const bool bGrouped_)
    {
        if (Projection_.Aggregate == EEntityQueryAggregate::None)
        {
            if (Projection_.Field.Type == EEntityQueryFieldType::EntityID
                && bGrouped_)
            {
                VariantArray _IDs;
                _IDs.reserve(Members_.size());
                for (const auto& _Member : Members_)
                {
                    _IDs.emplace_back(_Member.EntityID);
                }
                return PropertyValue(std::move(_IDs));
            }
            return Members_.empty()
                ? PropertyValue::Nil
                : ReadField(Members_.front(), Projection_.Field);
        }

        if (Projection_.Aggregate == EEntityQueryAggregate::Count)
        {
            unsigned long long _nCount = 0;
            if (Projection_.bCountAll)
            {
                _nCount = static_cast<unsigned long long>(Members_.size());
            }
            else
            {
                for (const auto& _Member : Members_)
                {
                    if (!IsNil(ReadField(_Member, Projection_.Field)))
                    {
                        ++_nCount;
                    }
                }
            }
            return PropertyValue(_nCount);
        }

        if (Projection_.Aggregate == EEntityQueryAggregate::Sum
            || Projection_.Aggregate == EEntityQueryAggregate::Average)
        {
            long double _Sum = 0;
            size_t _nCount = 0;
            for (const auto& _Member : Members_)
            {
                const auto _Value = ReadField(_Member, Projection_.Field);
                if (IsNil(_Value))
                {
                    continue;
                }
                const auto _Number = ToNumber(_Value);
                if (!_Number)
                {
                    throw std::invalid_argument(
                        "SUM and AVG require numeric Entity fields");
                }
                _Sum += ToFloating(*_Number);
                ++_nCount;
            }
            if (_nCount == 0)
            {
                return PropertyValue::Nil;
            }
            if (Projection_.Aggregate == EEntityQueryAggregate::Average)
            {
                _Sum /= static_cast<long double>(_nCount);
            }
            return PropertyValue(static_cast<double>(_Sum));
        }

        std::optional<PropertyValue> _Result;
        for (const auto& _Member : Members_)
        {
            auto _Value = ReadField(_Member, Projection_.Field);
            if (IsNil(_Value))
            {
                continue;
            }
            if (!_Result)
            {
                _Result = std::move(_Value);
                continue;
            }
            const auto _Order = CompareValues(_Value, *_Result);
            if (!_Order)
            {
                throw std::invalid_argument(
                    "MIN and MAX require comparable Entity fields");
            }
            const bool _bReplace =
                Projection_.Aggregate == EEntityQueryAggregate::Minimum
                    ? *_Order < 0
                    : *_Order > 0;
            if (_bReplace)
            {
                _Result = std::move(_Value);
            }
        }
        return _Result ? *_Result : PropertyValue::Nil;
    }
}

iCAX::Database::SEntityQueryResult
iCAX::Database::CRepository::Select(
    IN const SEntityQuery& Query_,
    IN const iCAX::Data::ObjectMap& Parameters_)
{
    if ((Query_.Skip || Query_.Take) && Query_.OrderBy.empty())
    {
        throw std::invalid_argument(
            "Entity query SKIP/TAKE requires ORDER BY");
    }
    const auto _nSkip =
        BindPageOperand(Query_.Skip, Parameters_, "SKIP");
    const auto _nTake = Query_.Take
        ? std::optional<std::uint64_t>(
            BindPageOperand(Query_.Take, Parameters_, "TAKE"))
        : std::nullopt;

    const auto _Projections = NormalizeProjections(Query_.Projections);
    const auto _pMeta = GetMetaRegistry();
    if (!_pMeta)
    {
        throw std::runtime_error("Entity query requires a MetaRegistry");
    }

    for (const auto& _Projection : _Projections)
    {
        if (!_Projection.bCountAll)
        {
            ValidateField(_Projection.Field, *_pMeta);
        }
    }
    for (const auto& _Field : Query_.GroupBy)
    {
        ValidateField(_Field, *_pMeta);
    }

    const bool _bGrouped =
        !Query_.GroupBy.empty() || HasAggregate(_Projections);
    if (_bGrouped)
    {
        for (size_t _Index = 1; _Index < _Projections.size(); ++_Index)
        {
            const auto& _Projection = _Projections[_Index];
            if (_Projection.Aggregate == EEntityQueryAggregate::None
                && !ContainsField(Query_.GroupBy, _Projection.Field))
            {
                throw std::invalid_argument(
                    "A non-aggregate Entity projection must appear in GROUP BY: "
                    + _Projection.Alias);
            }
        }
    }

    std::vector<std::optional<size_t>> _OrderProjectionIndices;
    _OrderProjectionIndices.reserve(Query_.OrderBy.size());
    for (const auto& _Order : Query_.OrderBy)
    {
        if (_Order.bUseProjection)
        {
            const auto _Projection = std::find_if(
                _Projections.begin(),
                _Projections.end(),
                [&_Order](IN const SEntityQueryProjection& Projection_)
                {
                    return Upper(Projection_.Alias)
                        == Upper(_Order.ProjectionAlias);
                });
            if (_Projection == _Projections.end())
            {
                throw std::invalid_argument(
                    "Entity query ORDER BY references an unknown projection: "
                    + _Order.ProjectionAlias);
            }
            _OrderProjectionIndices.push_back(
                static_cast<size_t>(
                    std::distance(_Projections.begin(), _Projection)));
            continue;
        }
        _OrderProjectionIndices.push_back(std::nullopt);
        ValidateField(_Order.Field, *_pMeta);
        if (_bGrouped && !ContainsField(Query_.GroupBy, _Order.Field))
        {
            throw std::invalid_argument(
                "Grouped Entity query ORDER BY field must appear in GROUP BY");
        }
    }

    std::vector<SSourceRow> _Sources;
    for (const auto& _EntityID : Query(Query_.Where, Parameters_))
    {
        const auto _pEntity = GetEntity(_EntityID);
        if (_pEntity)
        {
            _Sources.push_back({ _EntityID, _pEntity });
        }
    }

    std::vector<SGroup> _Groups;
    if (_bGrouped)
    {
        if (Query_.GroupBy.empty())
        {
            _Groups.push_back({ {}, _Sources });
        }
        else
        {
            for (const auto& _Source : _Sources)
            {
                PropertyArray _Key;
                _Key.reserve(Query_.GroupBy.size());
                for (const auto& _Field : Query_.GroupBy)
                {
                    _Key.push_back(ReadField(_Source, _Field));
                }
                auto _Group = std::find_if(
                    _Groups.begin(),
                    _Groups.end(),
                    [&_Key](IN const SGroup& Group_)
                    {
                        return KeysEqual(Group_.Key, _Key);
                    });
                if (_Group == _Groups.end())
                {
                    _Groups.push_back({ std::move(_Key), { _Source } });
                }
                else
                {
                    _Group->Members.push_back(_Source);
                }
            }
        }
    }
    else
    {
        _Groups.reserve(_Sources.size());
        for (const auto& _Source : _Sources)
        {
            _Groups.push_back({ {}, { _Source } });
        }
    }

    std::vector<SMaterializedRow> _Rows;
    _Rows.reserve(_Groups.size());
    for (auto& _Group : _Groups)
    {
        SMaterializedRow _Row;
        _Row.Members = std::move(_Group.Members);
        _Row.Values.reserve(_Projections.size());
        for (const auto& _Projection : _Projections)
        {
            _Row.Values.push_back(AggregateProjection(
                _Projection,
                _Row.Members,
                _bGrouped));
        }
        _Rows.push_back(std::move(_Row));
    }

    std::stable_sort(
        _Rows.begin(),
        _Rows.end(),
        [&Query_, &_OrderProjectionIndices](
            IN const SMaterializedRow& Left_,
            IN const SMaterializedRow& Right_)
        {
            for (size_t _Index = 0;
                _Index < Query_.OrderBy.size();
                ++_Index)
            {
                const auto& _Order = Query_.OrderBy[_Index];
                PropertyValue _Left;
                PropertyValue _Right;
                if (_Order.bUseProjection)
                {
                    const auto _nProjection =
                        *_OrderProjectionIndices[_Index];
                    _Left = Left_.Values[_nProjection];
                    _Right = Right_.Values[_nProjection];
                }
                else
                {
                    _Left = Left_.Members.empty()
                        ? PropertyValue::Nil
                        : ReadField(Left_.Members.front(), _Order.Field);
                    _Right = Right_.Members.empty()
                        ? PropertyValue::Nil
                        : ReadField(Right_.Members.front(), _Order.Field);
                }

                if (IsNil(_Left) || IsNil(_Right))
                {
                    if (IsNil(_Left) && IsNil(_Right))
                    {
                        continue;
                    }
                    // Nil 始终排在非空值之后，避免 DESC 时缺失字段跑到最前面。
                    return !IsNil(_Left);
                }
                const auto _Comparison = CompareValues(_Left, _Right);
                if (!_Comparison || *_Comparison == 0)
                {
                    continue;
                }
                return _Order.Direction
                    == EEntityQueryOrderDirection::Descending
                    ? *_Comparison > 0
                    : *_Comparison < 0;
            }
            return CompareMemberEntityIDs(Left_, Right_);
        });

    SEntityQueryResult _Result;
    _Result.TotalCount = static_cast<std::uint64_t>(_Rows.size());
    _Result.Columns.reserve(_Projections.size());
    for (const auto& _Projection : _Projections)
    {
        _Result.Columns.push_back(_Projection.Alias);
    }

    const auto _nFirst =
        _nSkip >= _Result.TotalCount
        ? _Rows.size()
        : static_cast<size_t>(_nSkip);
    const auto _nRemaining = _Rows.size() - _nFirst;
    const auto _nPageSize = _nTake
        ? static_cast<size_t>((std::min)(
            *_nTake,
            static_cast<std::uint64_t>(_nRemaining)))
        : _nRemaining;
    const auto _nLast = _nFirst + _nPageSize;

    _Result.Rows.reserve(_nPageSize);
    for (size_t _Index = _nFirst; _Index < _nLast; ++_Index)
    {
        auto& _Row = _Rows[_Index];
        for (const auto& _Member : _Row.Members)
        {
            _Result.EntityIDs.push_back(_Member.EntityID);
        }
        _Result.Rows.push_back(std::move(_Row.Values));
    }
    return _Result;
}
