#include "pch.h"

#include "EntityView.h"

#include "IEntity.h"
#include "IMetaRegistry.h"
#include "Repository.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <type_traits>

namespace
{
    using namespace iCAX::Data;
    using namespace iCAX::Database;

    std::string TopLevelPropertyName(IN const std::string& strPropertyPath_)
    {
        const auto _nSeparator = strPropertyPath_.find_first_of(".[");
        return strPropertyPath_.substr(0, _nSeparator);
    }

    std::optional<PropertyValue> ReadProperty(
        IN const IEntity& Entity_,
        IN const std::string& strComponentClass_,
        IN const std::string& strPropertyPath_)
    {
        const auto _pComponent = Entity_.GetComponent(strComponentClass_);
        if (!_pComponent)
        {
            return std::nullopt;
        }

        const auto _strTopLevelName = TopLevelPropertyName(strPropertyPath_);
        if (_strTopLevelName.empty())
        {
            return std::nullopt;
        }

        try
        {
            auto _Value = _pComponent->GetProperty(_strTopLevelName);
            if (_strTopLevelName.size() == strPropertyPath_.size())
            {
                return _Value;
            }

            auto _strNestedPath = strPropertyPath_.substr(_strTopLevelName.size());
            if (!_strNestedPath.empty() && _strNestedPath.front() == '.')
            {
                _strNestedPath.erase(_strNestedPath.begin());
            }
            return _Value.GetByPath(_strNestedPath);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

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
            else if constexpr (std::is_integral_v<TValue> && !std::is_same_v<TValue, bool>)
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
                    _Result.Unsigned = static_cast<unsigned long long>(Value_);
                }
                return _Result;
            }
            return std::nullopt;
        }, Value_.m_Value);
    }

    std::optional<int> CompareNumbers(
        IN const SNumericValue& Left_,
        IN const SNumericValue& Right_)
    {
        if (Left_.Kind == SNumericValue::EKind::FloatingPoint
            || Right_.Kind == SNumericValue::EKind::FloatingPoint)
        {
            const auto _ToFloating = [](IN const SNumericValue& Value_)
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
                return static_cast<long double>(0);
            };
            const auto _Left = _ToFloating(Left_);
            const auto _Right = _ToFloating(Right_);
            if (std::isnan(_Left) || std::isnan(_Right))
            {
                return std::nullopt;
            }
            if (_Left < _Right)
            {
                return -1;
            }
            if (_Left > _Right)
            {
                return 1;
            }
            return 0;
        }

        if (Left_.Kind == SNumericValue::EKind::SignedInteger
            && Right_.Kind == SNumericValue::EKind::SignedInteger)
        {
            return Left_.Signed < Right_.Signed ? -1 : Left_.Signed > Right_.Signed ? 1 : 0;
        }
        if (Left_.Kind == SNumericValue::EKind::UnsignedInteger
            && Right_.Kind == SNumericValue::EKind::UnsignedInteger)
        {
            return Left_.Unsigned < Right_.Unsigned ? -1 : Left_.Unsigned > Right_.Unsigned ? 1 : 0;
        }

        if (Left_.Kind == SNumericValue::EKind::SignedInteger)
        {
            if (Left_.Signed < 0)
            {
                return -1;
            }
            const auto _Left = static_cast<unsigned long long>(Left_.Signed);
            return _Left < Right_.Unsigned ? -1 : _Left > Right_.Unsigned ? 1 : 0;
        }

        if (Right_.Signed < 0)
        {
            return 1;
        }
        const auto _Right = static_cast<unsigned long long>(Right_.Signed);
        return Left_.Unsigned < _Right ? -1 : Left_.Unsigned > _Right ? 1 : 0;
    }

    bool ValuesEqual(
        IN const PropertyValue& Left_,
        IN const PropertyValue& Right_)
    {
        const auto _LeftNumber = ToNumber(Left_);
        const auto _RightNumber = ToNumber(Right_);
        if (_LeftNumber && _RightNumber)
        {
            const auto _Order = CompareNumbers(*_LeftNumber, *_RightNumber);
            return _Order && *_Order == 0;
        }
        return Left_ == Right_;
    }

    std::optional<int> CompareOrderedValues(
        IN const PropertyValue& Left_,
        IN const PropertyValue& Right_)
    {
        const auto _LeftNumber = ToNumber(Left_);
        const auto _RightNumber = ToNumber(Right_);
        if (_LeftNumber && _RightNumber)
        {
            return CompareNumbers(*_LeftNumber, *_RightNumber);
        }

        if (Left_.m_Value.index() != Right_.m_Value.index())
        {
            return std::nullopt;
        }

        try
        {
            if (Left_ < Right_)
            {
                return -1;
            }
            if (Left_ > Right_)
            {
                return 1;
            }
            return 0;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool EvaluateComparison(
        IN const PropertyValue& Left_,
        IN const EEntityWhereComparison Comparison_,
        IN const PropertyValue& Right_)
    {
        switch (Comparison_)
        {
        case EEntityWhereComparison::Equal:
            return ValuesEqual(Left_, Right_);
        case EEntityWhereComparison::NotEqual:
            return !ValuesEqual(Left_, Right_);
        case EEntityWhereComparison::Less:
        case EEntityWhereComparison::LessOrEqual:
        case EEntityWhereComparison::Greater:
        case EEntityWhereComparison::GreaterOrEqual:
        {
            const auto _Order = CompareOrderedValues(Left_, Right_);
            if (!_Order)
            {
                return false;
            }
            switch (Comparison_)
            {
            case EEntityWhereComparison::Less:
                return *_Order < 0;
            case EEntityWhereComparison::LessOrEqual:
                return *_Order <= 0;
            case EEntityWhereComparison::Greater:
                return *_Order > 0;
            case EEntityWhereComparison::GreaterOrEqual:
                return *_Order >= 0;
            default:
                return false;
            }
        }
        case EEntityWhereComparison::Contains:
            if (Left_.Is<std::string>() && Right_.Is<std::string>())
            {
                return Left_.To<std::string>().find(Right_.To<std::string>()) != std::string::npos;
            }
            if (Left_.Is<VariantArray>())
            {
                const auto& _Values = std::get<VariantArray>(Left_.m_Value);
                return std::any_of(_Values.begin(), _Values.end(), [&Right_](IN const auto& Value_)
                {
                    return ValuesEqual(Value_, Right_);
                });
            }
            if (Left_.Is<ObjectMap>() && Right_.Is<std::string>())
            {
                return std::get<ObjectMap>(Left_.m_Value).contains(Right_.To<std::string>());
            }
            return false;
        case EEntityWhereComparison::StartsWith:
            if (Left_.Is<std::string>() && Right_.Is<std::string>())
            {
                return Left_.To<std::string>().starts_with(Right_.To<std::string>());
            }
            return false;
        case EEntityWhereComparison::EndsWith:
            if (Left_.Is<std::string>() && Right_.Is<std::string>())
            {
                return Left_.To<std::string>().ends_with(Right_.To<std::string>());
            }
            return false;
        case EEntityWhereComparison::In:
            if (Right_.Is<VariantArray>())
            {
                const auto& _Values = std::get<VariantArray>(Right_.m_Value);
                return std::any_of(_Values.begin(), _Values.end(), [&Left_](IN const auto& Value_)
                {
                    return ValuesEqual(Left_, Value_);
                });
            }
            if (Right_.Is<ObjectMap>() && Left_.Is<std::string>())
            {
                return std::get<ObjectMap>(Right_.m_Value).contains(Left_.To<std::string>());
            }
            return false;
        }
        return false;
    }

    void CollectReferenceIDs(
        IN const PropertyValue& Value_,
        IN OUT std::set<uuid>& EntityIDs_)
    {
        if (Value_.Is<uuid>())
        {
            const auto _ID = Value_.To<uuid>();
            if (!_ID.is_nil())
            {
                EntityIDs_.insert(_ID);
            }
            return;
        }
        if (Value_.Is<std::string>())
        {
            const auto _ID = uuid::from_string(Value_.To<std::string>());
            if (_ID && !_ID->is_nil())
            {
                EntityIDs_.insert(*_ID);
            }
            return;
        }
        if (Value_.Is<VariantArray>())
        {
            for (const auto& _Item : std::get<VariantArray>(Value_.m_Value))
            {
                CollectReferenceIDs(_Item, EntityIDs_);
            }
        }
    }

    SEntityWhereNode NormalizeNode(IN const SEntityWhereNode& Node_)
    {
        switch (Node_.Type)
        {
        case EEntityWhereNodeType::Constant:
            return CEntityWhereBuilder::Constant(Node_.bConstant);
        case EEntityWhereNodeType::HasComponent:
            if (Node_.ComponentClass.empty())
            {
                throw std::invalid_argument("Entity where component class cannot be empty");
            }
            return CEntityWhereBuilder::HasComponent(Node_.ComponentClass);
        case EEntityWhereNodeType::ComponentEnabled:
            if (Node_.ComponentClass.empty())
            {
                throw std::invalid_argument("Entity where component class cannot be empty");
            }
            return CEntityWhereBuilder::ComponentEnabled(Node_.ComponentClass);
        case EEntityWhereNodeType::PropertyComparison:
            if (Node_.ComponentClass.empty() || TopLevelPropertyName(Node_.PropertyPath).empty())
            {
                throw std::invalid_argument("Entity where property comparison requires component class and property path");
            }
            if (Node_.Operand.Type == EEntityValueOperandType::Parameter
                && Node_.Operand.ParameterName.empty())
            {
                throw std::invalid_argument("Entity where parameter name cannot be empty");
            }
            return CEntityWhereBuilder::Compare(
                Node_.ComponentClass,
                Node_.PropertyPath,
                Node_.Comparison,
                Node_.Operand);
        case EEntityWhereNodeType::Not:
        {
            if (Node_.Children.size() != 1)
            {
                throw std::invalid_argument("Entity where NOT requires exactly one child");
            }
            auto _Child = NormalizeNode(Node_.Children.front());
            if (_Child.Type == EEntityWhereNodeType::Constant)
            {
                return CEntityWhereBuilder::Constant(!_Child.bConstant);
            }
            if (_Child.Type == EEntityWhereNodeType::Not)
            {
                return _Child.Children.front();
            }
            return CEntityWhereBuilder::Not(std::move(_Child));
        }
        case EEntityWhereNodeType::Reference:
            if (Node_.ComponentClass.empty()
                || TopLevelPropertyName(Node_.PropertyPath).empty()
                || Node_.Children.size() != 1)
            {
                throw std::invalid_argument(
                    "Entity where reference requires component class, property path and exactly one target predicate");
            }
            return CEntityWhereBuilder::Reference(
                Node_.ComponentClass,
                Node_.PropertyPath,
                NormalizeNode(Node_.Children.front()),
                Node_.ReferenceMatch);
        case EEntityWhereNodeType::All:
        case EEntityWhereNodeType::Any:
        {
            const bool _bAll = Node_.Type == EEntityWhereNodeType::All;
            std::vector<SEntityWhereNode> _Children;
            for (const auto& _RawChild : Node_.Children)
            {
                auto _Child = NormalizeNode(_RawChild);
                if (_Child.Type == EEntityWhereNodeType::Constant)
                {
                    if ((_bAll && !_Child.bConstant) || (!_bAll && _Child.bConstant))
                    {
                        return CEntityWhereBuilder::Constant(!_bAll);
                    }
                    continue;
                }
                if (_Child.Type == Node_.Type)
                {
                    _Children.insert(
                        _Children.end(),
                        std::make_move_iterator(_Child.Children.begin()),
                        std::make_move_iterator(_Child.Children.end()));
                }
                else
                {
                    _Children.push_back(std::move(_Child));
                }
            }

            std::sort(_Children.begin(), _Children.end());
            _Children.erase(std::unique(_Children.begin(), _Children.end()), _Children.end());
            for (const auto& _Child : _Children)
            {
                const auto _Negated = CEntityWhereBuilder::Not(_Child);
                if (std::binary_search(_Children.begin(), _Children.end(), _Negated))
                {
                    return CEntityWhereBuilder::Constant(!_bAll);
                }
            }
            if (_Children.empty())
            {
                return CEntityWhereBuilder::Constant(_bAll);
            }
            if (_Children.size() == 1)
            {
                return std::move(_Children.front());
            }
            return _bAll
                ? CEntityWhereBuilder::All(std::move(_Children))
                : CEntityWhereBuilder::Any(std::move(_Children));
        }
        }
        throw std::invalid_argument("Unknown Entity where node type");
    }

    void CollectParameterNames(
        IN const SEntityWhereNode& Node_,
        IN OUT std::set<std::string>& Names_)
    {
        if (Node_.Type == EEntityWhereNodeType::PropertyComparison
            && Node_.Operand.Type == EEntityValueOperandType::Parameter)
        {
            Names_.insert(Node_.Operand.ParameterName);
        }
        for (const auto& _Child : Node_.Children)
        {
            CollectParameterNames(_Child, Names_);
        }
    }
}

iCAX::Database::SEntityValueOperand
iCAX::Database::CEntityWhereBuilder::Literal(IN iCAX::Data::PropertyValue Value_)
{
    SEntityValueOperand _Operand;
    _Operand.Type = EEntityValueOperandType::Literal;
    _Operand.Literal = std::move(Value_);
    return _Operand;
}

iCAX::Database::SEntityValueOperand
iCAX::Database::CEntityWhereBuilder::Parameter(IN std::string ParameterName_)
{
    SEntityValueOperand _Operand;
    _Operand.Type = EEntityValueOperandType::Parameter;
    _Operand.ParameterName = std::move(ParameterName_);
    return _Operand;
}

iCAX::Database::SEntityWhereNode
iCAX::Database::CEntityWhereBuilder::Constant(IN const bool bValue_)
{
    SEntityWhereNode _Node;
    _Node.Type = EEntityWhereNodeType::Constant;
    _Node.bConstant = bValue_;
    return _Node;
}

iCAX::Database::SEntityWhereNode
iCAX::Database::CEntityWhereBuilder::HasComponent(IN std::string ComponentClass_)
{
    SEntityWhereNode _Node;
    _Node.Type = EEntityWhereNodeType::HasComponent;
    _Node.ComponentClass = std::move(ComponentClass_);
    return _Node;
}

iCAX::Database::SEntityWhereNode
iCAX::Database::CEntityWhereBuilder::ComponentEnabled(IN std::string ComponentClass_)
{
    SEntityWhereNode _Node;
    _Node.Type = EEntityWhereNodeType::ComponentEnabled;
    _Node.ComponentClass = std::move(ComponentClass_);
    return _Node;
}

iCAX::Database::SEntityWhereNode
iCAX::Database::CEntityWhereBuilder::Compare(
    IN std::string ComponentClass_,
    IN std::string PropertyPath_,
    IN const EEntityWhereComparison Comparison_,
    IN SEntityValueOperand Operand_)
{
    SEntityWhereNode _Node;
    _Node.Type = EEntityWhereNodeType::PropertyComparison;
    _Node.ComponentClass = std::move(ComponentClass_);
    _Node.PropertyPath = std::move(PropertyPath_);
    _Node.Comparison = Comparison_;
    _Node.Operand = std::move(Operand_);
    return _Node;
}

iCAX::Database::SEntityWhereNode
iCAX::Database::CEntityWhereBuilder::All(IN std::vector<SEntityWhereNode> Children_)
{
    SEntityWhereNode _Node;
    _Node.Type = EEntityWhereNodeType::All;
    _Node.Children = std::move(Children_);
    return _Node;
}

iCAX::Database::SEntityWhereNode
iCAX::Database::CEntityWhereBuilder::All(
    IN std::initializer_list<SEntityWhereNode> Children_)
{
    return All(std::vector<SEntityWhereNode>(Children_));
}

iCAX::Database::SEntityWhereNode
iCAX::Database::CEntityWhereBuilder::Any(IN std::vector<SEntityWhereNode> Children_)
{
    SEntityWhereNode _Node;
    _Node.Type = EEntityWhereNodeType::Any;
    _Node.Children = std::move(Children_);
    return _Node;
}

iCAX::Database::SEntityWhereNode
iCAX::Database::CEntityWhereBuilder::Any(
    IN std::initializer_list<SEntityWhereNode> Children_)
{
    return Any(std::vector<SEntityWhereNode>(Children_));
}

iCAX::Database::SEntityWhereNode
iCAX::Database::CEntityWhereBuilder::Not(IN SEntityWhereNode Child_)
{
    SEntityWhereNode _Node;
    _Node.Type = EEntityWhereNodeType::Not;
    _Node.Children.push_back(std::move(Child_));
    return _Node;
}

iCAX::Database::SEntityWhereNode
iCAX::Database::CEntityWhereBuilder::Reference(
    IN std::string ComponentClass_,
    IN std::string PropertyPath_,
    IN SEntityWhereNode TargetPredicate_,
    IN const EEntityReferenceMatch Match_)
{
    SEntityWhereNode _Node;
    _Node.Type = EEntityWhereNodeType::Reference;
    _Node.ComponentClass = std::move(ComponentClass_);
    _Node.PropertyPath = std::move(PropertyPath_);
    _Node.ReferenceMatch = Match_;
    _Node.Children.push_back(std::move(TargetPredicate_));
    return _Node;
}

iCAX::Database::SEntityWhere
iCAX::Database::CEntityWhereBuilder::Build(IN SEntityWhereNode Root_)
{
    return { std::move(Root_) };
}

iCAX::Database::SEntityWhere
iCAX::Database::CEntityWhereBuilder::MatchAll()
{
    return Build(Constant(true));
}

iCAX::Database::SEntityWhere
iCAX::Database::CEntityWhereBuilder::Normalize(IN const SEntityWhere& Where_)
{
    return Build(NormalizeNode(Where_.Root));
}

iCAX::Data::ObjectMap
iCAX::Database::CEntityWhereBuilder::BindParameters(
    IN const SEntityWhere& Where_,
    IN const iCAX::Data::ObjectMap& Parameters_)
{
    std::set<std::string> _Names;
    CollectParameterNames(Where_.Root, _Names);

    iCAX::Data::ObjectMap _Bound;
    for (const auto& _Name : _Names)
    {
        const auto _Parameter = Parameters_.find(_Name);
        if (_Parameter == Parameters_.end())
        {
            throw std::invalid_argument("Entity where parameter is missing: " + _Name);
        }
        _Bound.emplace(_Name, _Parameter->second);
    }
    return _Bound;
}

iCAX::Database::CEntityWhereEvaluator::CEntityWhereEvaluator(
    IN std::shared_ptr<CRepository> pRepository_,
    IN SEntityWhere Where_,
    IN iCAX::Data::ObjectMap Parameters_)
    : m_pRepository(std::move(pRepository_))
    , m_Where(std::move(Where_))
    , m_Parameters(std::move(Parameters_))
{
}

const iCAX::Database::SEntityWhere&
iCAX::Database::CEntityWhereEvaluator::GetWhere() const
{
    return m_Where;
}

const iCAX::Data::ObjectMap&
iCAX::Database::CEntityWhereEvaluator::GetParameters() const
{
    return m_Parameters;
}

iCAX::Database::CEntityView::CEntityView(
    IN std::shared_ptr<CRepository> pRepository_,
    IN SEntityWhere Where_,
    IN iCAX::Data::ObjectMap Parameters_)
    : m_pRepository(pRepository_)
    , m_Evaluator(
        std::move(pRepository_),
        std::move(Where_),
        std::move(Parameters_))
{
}

void iCAX::Database::CEntityView::Initialize()
{
    RefreshFromRepository();
}

void iCAX::Database::CEntityView::RefreshFromRepository()
{
    const auto _pRepository = m_pRepository.lock();
    if (!_pRepository)
    {
        throw std::logic_error("Entity view repository is unavailable");
    }

    std::set<iCAX::Data::uuid> _EntityIDs;
    std::map<
        iCAX::Data::uuid,
        std::map<iCAX::Data::uuid, SDependencySet>> _DependenciesByEntity;
    std::map<iCAX::Data::uuid, std::set<iCAX::Data::uuid>> _EntitiesByDependency;
    for (const auto& _EntityID : _pRepository->GetEntityIDs())
    {
        auto _Evaluation = m_Evaluator.EvaluateEntity(_EntityID);
        if (_Evaluation.bMatches)
        {
            _EntityIDs.insert(_EntityID);
        }
        if (!_Evaluation.Dependencies.empty())
        {
            _DependenciesByEntity.emplace(_EntityID, _Evaluation.Dependencies);
            for (const auto& [_DependencyID, _] : _Evaluation.Dependencies)
            {
                _EntitiesByDependency[_DependencyID].insert(_EntityID);
            }
        }
    }

    EntityViewEventArgs _EventArgs;
    bool _bPublish = false;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        if (m_nRevision == 0)
        {
            m_nRevision = 1;
        }
        else if (m_EntityIDs != _EntityIDs)
        {
            _EventArgs.nPreviousRevision = m_nRevision;
            for (const auto& _EntityID : _EntityIDs)
            {
                if (!m_EntityIDs.contains(_EntityID))
                {
                    _EventArgs.AddedEntityIDs.push_back(_EntityID);
                }
            }
            for (const auto& _EntityID : m_EntityIDs)
            {
                if (!_EntityIDs.contains(_EntityID))
                {
                    _EventArgs.RemovedEntityIDs.push_back(_EntityID);
                }
            }
            _EventArgs.nRevision = ++m_nRevision;
            _bPublish = true;
        }
        m_EntityIDs = std::move(_EntityIDs);
        m_DependenciesByEntity = std::move(_DependenciesByEntity);
        m_EntitiesByDependency = std::move(_EntitiesByDependency);
    }

    if (_bPublish)
    {
        PublishChanged(std::move(_EventArgs));
    }
}

const iCAX::Database::SEntityWhere&
iCAX::Database::CEntityView::GetWhere() const
{
    return m_Evaluator.GetWhere();
}

const iCAX::Data::ObjectMap&
iCAX::Database::CEntityView::GetParameters() const
{
    return m_Evaluator.GetParameters();
}

uint64_t iCAX::Database::CEntityView::GetRevision() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_nRevision;
}

std::vector<iCAX::Data::uuid>
iCAX::Database::CEntityView::GetEntityIDs() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return { m_EntityIDs.begin(), m_EntityIDs.end() };
}

bool iCAX::Database::CEntityView::Contains(
    IN const iCAX::Data::uuid& EntityID_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_EntityIDs.contains(EntityID_);
}

void iCAX::Database::CEntityView::AddObserver(
    IN std::shared_ptr<IEntityViewEventListener> Observer_)
{
    if (!Observer_)
    {
        return;
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Observers.push_back(std::move(Observer_));
}

void iCAX::Database::CEntityView::RemoveObserver(
    IN std::shared_ptr<IEntityViewEventListener> Observer_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Observers.remove_if(
        [&](const std::weak_ptr<IEntityViewEventListener>& WeakObserver_)
        {
            return WeakObserver_.expired() || WeakObserver_.lock() == Observer_;
        });
}

void iCAX::Database::CEntityView::OnRepositoryChanging(
    IN void*,
    IN const RepositoryEventArgs&)
{
}

void iCAX::Database::CEntityView::OnRepositoryChanged(
    IN void*,
    IN const RepositoryEventArgs& Args_)
{
    std::set<iCAX::Data::uuid> _AffectedEntityIDs;
    if (Args_.nType == RepositoryEventArgs::kBatchChanged)
    {
        if (!Args_.pBatch)
        {
            return;
        }
        for (const auto& _Record : Args_.pBatch->Records)
        {
            CollectAffectedEntityIDs(
                _Record.nType,
                _Record.EntityID,
                _Record.strClassName,
                _Record.PreviousProperties,
                _Record.NewProperties,
                _AffectedEntityIDs);
        }
    }
    else
    {
        CollectAffectedEntityIDs(
            Args_.nType,
            Args_.EntityID,
            Args_.strClassName,
            Args_.PreviousProperties,
            Args_.NewProperties,
            _AffectedEntityIDs);
    }

    ApplyEntityChanges(_AffectedEntityIDs);
}

iCAX::Database::CEntityWhereEvaluator::SEvaluationResult
iCAX::Database::CEntityWhereEvaluator::EvaluateEntity(
    IN const iCAX::Data::uuid& EntityID_) const
{
    SEvaluationResult _Result;
    const auto _pRepository = m_pRepository.lock();
    const auto _pEntity = _pRepository ? _pRepository->GetEntity(EntityID_) : nullptr;
    if (_pEntity)
    {
        _Result.bMatches = EvaluateNode(
            m_Where.Root,
            *_pEntity,
            _Result.Dependencies);
    }
    return _Result;
}

bool iCAX::Database::CEntityWhereEvaluator::EvaluateNode(
    IN const SEntityWhereNode& Node_,
    IN const IEntity& Entity_,
    IN OUT std::map<iCAX::Data::uuid, SDependencySet>& Dependencies_) const
{
    auto& _EntityDependencies = Dependencies_[Entity_.GetID()];
    switch (Node_.Type)
    {
    case EEntityWhereNodeType::Constant:
        return Node_.bConstant;
    case EEntityWhereNodeType::HasComponent:
        _EntityDependencies.ComponentPresence.insert(Node_.ComponentClass);
        return Entity_.HasComponent(Node_.ComponentClass);
    case EEntityWhereNodeType::ComponentEnabled:
    {
        _EntityDependencies.ComponentPresence.insert(Node_.ComponentClass);
        _EntityDependencies.ComponentState.insert(Node_.ComponentClass);
        const auto _pComponent = Entity_.GetComponent(Node_.ComponentClass);
        return _pComponent && _pComponent->IsEnable();
    }
    case EEntityWhereNodeType::PropertyComparison:
    {
        const auto _strPropertyName = TopLevelPropertyName(Node_.PropertyPath);
        _EntityDependencies.ComponentPresence.insert(Node_.ComponentClass);
        _EntityDependencies.Properties[Node_.ComponentClass].insert(_strPropertyName);
        const auto _Value = ReadProperty(
            Entity_,
            Node_.ComponentClass,
            Node_.PropertyPath);
        if (!_Value)
        {
            return false;
        }

        const auto _pRepository = m_pRepository.lock();
        const auto _pMeta = _pRepository ? _pRepository->GetMetaRegistry() : nullptr;
        if (_pMeta
            && _pMeta->HasPropertyByName(Node_.ComponentClass, _strPropertyName)
            && _pMeta->IsDerivedPropertyByName(Node_.ComponentClass, _strPropertyName))
        {
            for (const auto& _Source : _pRepository->GetDerivedPropertyDependencies({
                Entity_.GetID(),
                Node_.ComponentClass,
                _strPropertyName,
            }))
            {
                auto& _SourceDependencies = Dependencies_[_Source.EntityID];
                _SourceDependencies.ComponentPresence.insert(_Source.ComponentClass);
                _SourceDependencies.Properties[_Source.ComponentClass].insert(_Source.PropertyName);
            }
        }

        const PropertyValue* _pRight = &Node_.Operand.Literal;
        if (Node_.Operand.Type == EEntityValueOperandType::Parameter)
        {
            const auto _Parameter = m_Parameters.find(Node_.Operand.ParameterName);
            if (_Parameter == m_Parameters.end())
            {
                return false;
            }
            _pRight = &_Parameter->second;
        }
        return EvaluateComparison(*_Value, Node_.Comparison, *_pRight);
    }
    case EEntityWhereNodeType::All:
    {
        bool _bResult = true;
        for (const auto& _Child : Node_.Children)
        {
            const bool _bChild = EvaluateNode(_Child, Entity_, Dependencies_);
            _bResult = _bResult && _bChild;
        }
        return _bResult;
    }
    case EEntityWhereNodeType::Any:
    {
        bool _bResult = false;
        for (const auto& _Child : Node_.Children)
        {
            const bool _bChild = EvaluateNode(_Child, Entity_, Dependencies_);
            _bResult = _bResult || _bChild;
        }
        return _bResult;
    }
    case EEntityWhereNodeType::Not:
        return !EvaluateNode(Node_.Children.front(), Entity_, Dependencies_);
    case EEntityWhereNodeType::Reference:
    {
        const auto _strPropertyName = TopLevelPropertyName(Node_.PropertyPath);
        _EntityDependencies.ComponentPresence.insert(Node_.ComponentClass);
        _EntityDependencies.Properties[Node_.ComponentClass].insert(_strPropertyName);
        const auto _Value = ReadProperty(
            Entity_,
            Node_.ComponentClass,
            Node_.PropertyPath);
        if (!_Value)
        {
            return false;
        }

        const auto _pRepository = m_pRepository.lock();
        const auto _pMeta = _pRepository ? _pRepository->GetMetaRegistry() : nullptr;
        if (_pMeta
            && _pMeta->HasPropertyByName(Node_.ComponentClass, _strPropertyName)
            && _pMeta->IsDerivedPropertyByName(Node_.ComponentClass, _strPropertyName))
        {
            for (const auto& _Source : _pRepository->GetDerivedPropertyDependencies({
                Entity_.GetID(),
                Node_.ComponentClass,
                _strPropertyName,
            }))
            {
                auto& _SourceDependencies = Dependencies_[_Source.EntityID];
                _SourceDependencies.ComponentPresence.insert(_Source.ComponentClass);
                _SourceDependencies.Properties[_Source.ComponentClass].insert(_Source.PropertyName);
            }
        }

        std::set<iCAX::Data::uuid> _ReferenceIDs;
        CollectReferenceIDs(*_Value, _ReferenceIDs);
        if (_ReferenceIDs.empty())
        {
            return false;
        }

        bool _bAny = false;
        bool _bAll = true;
        for (const auto& _ReferenceID : _ReferenceIDs)
        {
            Dependencies_[_ReferenceID].bEntityExistence = true;
            const auto _pTarget = _pRepository ? _pRepository->GetEntity(_ReferenceID) : nullptr;
            const bool _bTargetMatches = _pTarget
                && EvaluateNode(Node_.Children.front(), *_pTarget, Dependencies_);
            _bAny = _bAny || _bTargetMatches;
            _bAll = _bAll && _bTargetMatches;
        }
        return Node_.ReferenceMatch == EEntityReferenceMatch::Any ? _bAny : _bAll;
    }
    }
    return false;
}

bool iCAX::Database::CEntityView::EventAffects(
    IN const RepositoryEventArgs::EventType Type_,
    IN const std::string& strClassName_,
    IN const iCAX::Data::PropertySet& PreviousProperties_,
    IN const iCAX::Data::PropertySet& NewProperties_,
    IN const SDependencySet& Dependencies_) const
{
    if (Type_ == RepositoryEventArgs::kAddEntity
        || Type_ == RepositoryEventArgs::kDeleteEntity)
    {
        return true;
    }
    if (Type_ == RepositoryEventArgs::kAddComponent
        || Type_ == RepositoryEventArgs::kRemoveComponent)
    {
        return Dependencies_.ComponentPresence.contains(strClassName_);
    }
    if (Type_ == RepositoryEventArgs::kEnableComponent
        || Type_ == RepositoryEventArgs::kDisableComponent)
    {
        return Dependencies_.ComponentState.contains(strClassName_);
    }
    if (Type_ != RepositoryEventArgs::kModifyComponent)
    {
        return false;
    }

    const auto _Properties = Dependencies_.Properties.find(strClassName_);
    if (_Properties == Dependencies_.Properties.end())
    {
        return false;
    }
    if (PreviousProperties_.empty() && NewProperties_.empty())
    {
        return true;
    }
    for (const auto& [_Name, _] : PreviousProperties_)
    {
        if (_Properties->second.contains(_Name))
        {
            return true;
        }
    }
    for (const auto& [_Name, _] : NewProperties_)
    {
        if (_Properties->second.contains(_Name))
        {
            return true;
        }
    }
    return false;
}

void iCAX::Database::CEntityView::CollectAffectedEntityIDs(
    IN const RepositoryEventArgs::EventType Type_,
    IN const iCAX::Data::uuid& EntityID_,
    IN const std::string& strClassName_,
    IN const iCAX::Data::PropertySet& PreviousProperties_,
    IN const iCAX::Data::PropertySet& NewProperties_,
    IN OUT std::set<iCAX::Data::uuid>& EntityIDs_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (Type_ == RepositoryEventArgs::kAddEntity
        || Type_ == RepositoryEventArgs::kDeleteEntity)
    {
        EntityIDs_.insert(EntityID_);
    }

    const auto _Dependents = m_EntitiesByDependency.find(EntityID_);
    if (_Dependents == m_EntitiesByDependency.end())
    {
        return;
    }
    for (const auto& _DependentID : _Dependents->second)
    {
        const auto _CandidateDependencies = m_DependenciesByEntity.find(_DependentID);
        if (_CandidateDependencies == m_DependenciesByEntity.end())
        {
            continue;
        }
        const auto _Dependency = _CandidateDependencies->second.find(EntityID_);
        if (_Dependency != _CandidateDependencies->second.end()
            && EventAffects(
                Type_,
                strClassName_,
                PreviousProperties_,
                NewProperties_,
                _Dependency->second))
        {
            EntityIDs_.insert(_DependentID);
        }
    }
}

void iCAX::Database::CEntityView::ReplaceDependencies(
    IN const iCAX::Data::uuid& EntityID_,
    IN const std::map<iCAX::Data::uuid, SDependencySet>& Dependencies_)
{
    const auto _Previous = m_DependenciesByEntity.find(EntityID_);
    if (_Previous != m_DependenciesByEntity.end())
    {
        for (const auto& [_DependencyID, _] : _Previous->second)
        {
            auto _Dependents = m_EntitiesByDependency.find(_DependencyID);
            if (_Dependents == m_EntitiesByDependency.end())
            {
                continue;
            }
            _Dependents->second.erase(EntityID_);
            if (_Dependents->second.empty())
            {
                m_EntitiesByDependency.erase(_Dependents);
            }
        }
        m_DependenciesByEntity.erase(_Previous);
    }

    if (Dependencies_.empty())
    {
        return;
    }
    m_DependenciesByEntity.emplace(EntityID_, Dependencies_);
    for (const auto& [_DependencyID, _] : Dependencies_)
    {
        m_EntitiesByDependency[_DependencyID].insert(EntityID_);
    }
}

void iCAX::Database::CEntityView::ApplyEntityChanges(
    IN const std::set<iCAX::Data::uuid>& EntityIDs_)
{
    if (EntityIDs_.empty())
    {
        return;
    }

    std::map<iCAX::Data::uuid, SEvaluationResult> _Evaluations;
    for (const auto& _EntityID : EntityIDs_)
    {
        _Evaluations.emplace(_EntityID, m_Evaluator.EvaluateEntity(_EntityID));
    }

    EntityViewEventArgs _EventArgs;
    bool _bPublish = false;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _EventArgs.nPreviousRevision = m_nRevision;
        for (const auto& [_EntityID, _Evaluation] : _Evaluations)
        {
            ReplaceDependencies(_EntityID, _Evaluation.Dependencies);
            const bool _bContains = m_EntityIDs.contains(_EntityID);
            if (_Evaluation.bMatches && !_bContains)
            {
                m_EntityIDs.insert(_EntityID);
                _EventArgs.AddedEntityIDs.push_back(_EntityID);
            }
            else if (!_Evaluation.bMatches && _bContains)
            {
                m_EntityIDs.erase(_EntityID);
                _EventArgs.RemovedEntityIDs.push_back(_EntityID);
            }
        }

        if (!_EventArgs.AddedEntityIDs.empty()
            || !_EventArgs.RemovedEntityIDs.empty())
        {
            _EventArgs.nRevision = ++m_nRevision;
            _bPublish = true;
        }
    }

    if (_bPublish)
    {
        PublishChanged(std::move(_EventArgs));
    }
}

void iCAX::Database::CEntityView::PublishChanged(
    IN EntityViewEventArgs Args_)
{
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        m_PendingEvents.push_back(std::move(Args_));
        if (m_bPublishingEvents)
        {
            return;
        }
        m_bPublishingEvents = true;
    }

    for (;;)
    {
        EntityViewEventArgs _EventArgs;
        std::vector<std::shared_ptr<IEntityViewEventListener>> _Observers;
        {
            std::lock_guard<std::mutex> _Lock(m_Mutex);
            if (m_PendingEvents.empty())
            {
                m_bPublishingEvents = false;
                return;
            }

            _EventArgs = std::move(m_PendingEvents.front());
            m_PendingEvents.pop_front();
            for (auto _Observer = m_Observers.begin(); _Observer != m_Observers.end();)
            {
                if (auto _pObserver = _Observer->lock())
                {
                    _Observers.push_back(std::move(_pObserver));
                    ++_Observer;
                }
                else
                {
                    _Observer = m_Observers.erase(_Observer);
                }
            }
        }

        for (const auto& _Observer : _Observers)
        {
            try
            {
                _Observer->OnEntityViewChanged(this, _EventArgs);
            }
            catch (...)
            {
                // EntityView observers are notification-only. A failed observer
                // must not affect committed Repository data or other observers.
            }
        }
    }
}
