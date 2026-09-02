#include "pch.h"
#include "SectionRuleEngine.h"

namespace iCAX::ExtrusionRecognition
{
namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    using iCAX::GeometryData::Direction2;
    using iCAX::GeometryData::Point2;

    std::optional<double> ToNumber(IN const Variant& Value_)
    {
        return std::visit([](IN const auto& Value_) -> std::optional<double> {
            using TValue = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_arithmetic_v<TValue>)
            {
                return static_cast<double>(Value_);
            }
            return std::nullopt;
        }, Value_.m_Value);
    }

    bool ToBool(IN const Variant& Value_, OUT bool& bValue_)
    {
        if (Value_.Is<bool>())
        {
            bValue_ = Value_.To<bool>();
            return true;
        }
        if (const auto _Number = ToNumber(Value_))
        {
            bValue_ = std::abs(*_Number) > 1.0e-15;
            return true;
        }
        return false;
    }

    std::optional<std::string> ToString(IN const Variant& Value_)
    {
        return Value_.Is<std::string>()
            ? std::optional<std::string>(Value_.To<std::string>())
            : std::nullopt;
    }

    ObjectMap MakePoint(IN const Point2& Point_)
    {
        return { { "x", Point_.X }, { "y", Point_.Y } };
    }

    ObjectMap MakeDirection(IN const Direction2& Direction_)
    {
        return { { "x", Direction_.X }, { "y", Direction_.Y } };
    }

    bool TryReadPoint(IN const Variant& Value_, OUT Point2& Point_)
    {
        if (!Value_.Is<ObjectMap>())
        {
            return false;
        }
        const auto _Object = Value_.To<ObjectMap>();
        const auto _X = _Object.find("x");
        const auto _Y = _Object.find("y");
        if (_X == _Object.end() || _Y == _Object.end())
        {
            return false;
        }
        const auto _dX = ToNumber(_X->second);
        const auto _dY = ToNumber(_Y->second);
        if (!_dX || !_dY)
        {
            return false;
        }
        Point_ = { *_dX, *_dY };
        return true;
    }

    bool TryReadDirection(IN const Variant& Value_, OUT Direction2& Direction_)
    {
        Point2 _Point;
        if (!TryReadPoint(Value_, _Point))
        {
            return false;
        }
        const auto _Length = std::hypot(_Point.X, _Point.Y);
        if (_Length <= 1.0e-12)
        {
            return false;
        }
        Direction_ = { _Point.X / _Length, _Point.Y / _Length };
        return true;
    }

    double SignedArea(IN const std::vector<Point2>& Points_)
    {
        if (Points_.size() < 3)
        {
            return 0.0;
        }
        double _Area = 0.0;
        for (std::size_t _Index = 0; _Index < Points_.size(); ++_Index)
        {
            const auto& _A = Points_[_Index];
            const auto& _B = Points_[(_Index + 1) % Points_.size()];
            _Area += _A.X * _B.Y - _B.X * _A.Y;
        }
        return 0.5 * _Area;
    }

    const SSectionLoop2* FindLoop(
        IN const SSectionSnapshot& Section_,
        IN const std::string& strSelector_)
    {
        if (strSelector_ == "outer")
        {
            const auto _Iter = std::find_if(
                Section_.Loops.begin(),
                Section_.Loops.end(),
                [](IN const SSectionLoop2& Loop_) { return !Loop_.bInner; });
            return _Iter == Section_.Loops.end() ? nullptr : &*_Iter;
        }
        constexpr std::string_view kInnerPrefix = "inner:";
        if (strSelector_.starts_with(kInnerPrefix))
        {
            std::size_t _Wanted = 0;
            try
            {
                _Wanted = static_cast<std::size_t>(std::stoull(
                    strSelector_.substr(kInnerPrefix.size())));
            }
            catch (...)
            {
                return nullptr;
            }
            std::size_t _Current = 0;
            for (const auto& _Loop : Section_.Loops)
            {
                if (!_Loop.bInner)
                {
                    continue;
                }
                if (_Current++ == _Wanted)
                {
                    return &_Loop;
                }
            }
        }
        return nullptr;
    }

    std::pair<Direction2, Direction2> PrincipalAxes(
        IN const std::vector<Point2>& Points_)
    {
        if (Points_.empty())
        {
            return { { 1.0, 0.0 }, { 0.0, 1.0 } };
        }
        Point2 _Center;
        for (const auto& _Point : Points_)
        {
            _Center.X += _Point.X;
            _Center.Y += _Point.Y;
        }
        _Center.X /= static_cast<double>(Points_.size());
        _Center.Y /= static_cast<double>(Points_.size());

        double _XX = 0.0;
        double _XY = 0.0;
        double _YY = 0.0;
        for (const auto& _Point : Points_)
        {
            const auto _X = _Point.X - _Center.X;
            const auto _Y = _Point.Y - _Center.Y;
            _XX += _X * _X;
            _XY += _X * _Y;
            _YY += _Y * _Y;
        }
        const auto _Angle = 0.5 * std::atan2(2.0 * _XY, _XX - _YY);
        Direction2 _Long = { std::cos(_Angle), std::sin(_Angle) };
        Direction2 _Short = { -_Long.Y, _Long.X };
        return { _Long, _Short };
    }

    ObjectMap FitRectangle(
        IN const SSectionLoop2* pLoop_,
        IN double dTolerance_)
    {
        ObjectMap _Result{ { "valid", false } };
        if (!pLoop_ || pLoop_->Points.size() < 4)
        {
            return _Result;
        }
        Direction2 _AxisA;
        Direction2 _AxisB{ 0.0, 1.0 };
        auto _MinA = 0.0;
        auto _MaxA = 0.0;
        auto _MinB = 0.0;
        auto _MaxB = 0.0;
        auto _BestArea = (std::numeric_limits<double>::max)();
        for (std::size_t _Index = 0; _Index < pLoop_->Points.size(); ++_Index)
        {
            const auto& _Start = pLoop_->Points[_Index];
            const auto& _End = pLoop_->Points[(_Index + 1) % pLoop_->Points.size()];
            const auto _DX = _End.X - _Start.X;
            const auto _DY = _End.Y - _Start.Y;
            const auto _Length = std::hypot(_DX, _DY);
            if (_Length <= dTolerance_)
            {
                continue;
            }
            const Direction2 _CandidateA{ _DX / _Length, _DY / _Length };
            const Direction2 _CandidateB{ -_CandidateA.Y, _CandidateA.X };
            auto _CandidateMinA = (std::numeric_limits<double>::max)();
            auto _CandidateMaxA = (std::numeric_limits<double>::lowest)();
            auto _CandidateMinB = (std::numeric_limits<double>::max)();
            auto _CandidateMaxB = (std::numeric_limits<double>::lowest)();
            for (const auto& _Point : pLoop_->Points)
            {
                const auto _A = _Point.X * _CandidateA.X
                    + _Point.Y * _CandidateA.Y;
                const auto _B = _Point.X * _CandidateB.X
                    + _Point.Y * _CandidateB.Y;
                _CandidateMinA = std::min(_CandidateMinA, _A);
                _CandidateMaxA = std::max(_CandidateMaxA, _A);
                _CandidateMinB = std::min(_CandidateMinB, _B);
                _CandidateMaxB = std::max(_CandidateMaxB, _B);
            }
            const auto _Area = (_CandidateMaxA - _CandidateMinA)
                * (_CandidateMaxB - _CandidateMinB);
            if (_Area < _BestArea)
            {
                _BestArea = _Area;
                _AxisA = _CandidateA;
                _AxisB = _CandidateB;
                _MinA = _CandidateMinA;
                _MaxA = _CandidateMaxA;
                _MinB = _CandidateMinB;
                _MaxB = _CandidateMaxB;
            }
        }
        const auto _WidthA = _MaxA - _MinA;
        const auto _WidthB = _MaxB - _MinB;
        if (_WidthA <= dTolerance_ || _WidthB <= dTolerance_)
        {
            return _Result;
        }
        double _MaximumBoundaryError = 0.0;
        for (const auto& _Point : pLoop_->Points)
        {
            const auto _A = _Point.X * _AxisA.X + _Point.Y * _AxisA.Y;
            const auto _B = _Point.X * _AxisB.X + _Point.Y * _AxisB.Y;
            const auto _BoundaryError = std::min({
                std::abs(_A - _MinA),
                std::abs(_A - _MaxA),
                std::abs(_B - _MinB),
                std::abs(_B - _MaxB)
            });
            _MaximumBoundaryError = std::max(_MaximumBoundaryError, _BoundaryError);
        }
        const auto _Area = std::abs(SignedArea(pLoop_->Points));
        const auto _BoxArea = _WidthA * _WidthB;
        const auto _AreaError = std::abs(_BoxArea - _Area) / _BoxArea;
        const auto _AllowedBoundaryError = std::max(
            dTolerance_,
            1.0e-4 * std::hypot(_WidthA, _WidthB));
        const bool _Valid = _MaximumBoundaryError <= _AllowedBoundaryError
            && _AreaError <= 5.0e-3;

        const auto _CenterA = 0.5 * (_MinA + _MaxA);
        const auto _CenterB = 0.5 * (_MinB + _MaxB);
        const Point2 _Center = {
            _CenterA * _AxisA.X + _CenterB * _AxisB.X,
            _CenterA * _AxisA.Y + _CenterB * _AxisB.Y
        };
        const bool _AIsLong = _WidthA >= _WidthB;
        _Result["valid"] = _Valid;
        _Result["center"] = MakePoint(_Center);
        _Result["width"] = std::max(_WidthA, _WidthB);
        _Result["height"] = std::min(_WidthA, _WidthB);
        _Result["longAxis"] = MakeDirection(_AIsLong ? _AxisA : _AxisB);
        _Result["shortAxis"] = MakeDirection(_AIsLong ? _AxisB : _AxisA);
        _Result["boundaryError"] = _MaximumBoundaryError;
        _Result["areaError"] = _AreaError;
        return _Result;
    }

    ObjectMap FitCircle(
        IN const SSectionLoop2* pLoop_,
        IN double dTolerance_)
    {
        ObjectMap _Result{ { "valid", false } };
        if (!pLoop_ || pLoop_->Points.size() < 6)
        {
            return _Result;
        }
        Point2 _Origin;
        for (const auto& _Point : pLoop_->Points)
        {
            _Origin.X += _Point.X;
            _Origin.Y += _Point.Y;
        }
        _Origin.X /= static_cast<double>(pLoop_->Points.size());
        _Origin.Y /= static_cast<double>(pLoop_->Points.size());

        double _Normal[3][4]{};
        for (const auto& _Point : pLoop_->Points)
        {
            const auto _X = _Point.X - _Origin.X;
            const auto _Y = _Point.Y - _Origin.Y;
            const auto _R2 = _X * _X + _Y * _Y;
            const double _Row[3]{ _X, _Y, 1.0 };
            for (std::size_t _I = 0; _I < 3; ++_I)
            {
                for (std::size_t _J = 0; _J < 3; ++_J)
                {
                    _Normal[_I][_J] += _Row[_I] * _Row[_J];
                }
                _Normal[_I][3] -= _Row[_I] * _R2;
            }
        }

        for (std::size_t _Column = 0; _Column < 3; ++_Column)
        {
            auto _Pivot = _Column;
            for (std::size_t _Row = _Column + 1; _Row < 3; ++_Row)
            {
                if (std::abs(_Normal[_Row][_Column])
                    > std::abs(_Normal[_Pivot][_Column]))
                {
                    _Pivot = _Row;
                }
            }
            if (std::abs(_Normal[_Pivot][_Column]) <= 1.0e-15)
            {
                return _Result;
            }
            if (_Pivot != _Column)
            {
                for (std::size_t _J = _Column; _J < 4; ++_J)
                {
                    std::swap(_Normal[_Pivot][_J], _Normal[_Column][_J]);
                }
            }
            const auto _Divisor = _Normal[_Column][_Column];
            for (std::size_t _J = _Column; _J < 4; ++_J)
            {
                _Normal[_Column][_J] /= _Divisor;
            }
            for (std::size_t _Row = 0; _Row < 3; ++_Row)
            {
                if (_Row == _Column) continue;
                const auto _Factor = _Normal[_Row][_Column];
                for (std::size_t _J = _Column; _J < 4; ++_J)
                {
                    _Normal[_Row][_J] -= _Factor * _Normal[_Column][_J];
                }
            }
        }

        const auto _A = _Normal[0][3];
        const auto _B = _Normal[1][3];
        const auto _C = _Normal[2][3];
        const Point2 _Center{
            _Origin.X - 0.5 * _A,
            _Origin.Y - 0.5 * _B
        };
        const auto _RadiusSquared = 0.25 * (_A * _A + _B * _B) - _C;
        if (_RadiusSquared <= dTolerance_ * dTolerance_)
        {
            return _Result;
        }
        const auto _Radius = std::sqrt(_RadiusSquared);
        double _MaximumError = 0.0;
        for (const auto& _Point : pLoop_->Points)
        {
            _MaximumError = std::max(
                _MaximumError,
                std::abs(std::hypot(
                    _Point.X - _Center.X,
                    _Point.Y - _Center.Y) - _Radius));
        }
        const auto _AllowedError = std::max(dTolerance_, _Radius * 1.0e-3);
        _Result["valid"] = _Radius > dTolerance_ && _MaximumError <= _AllowedError;
        _Result["center"] = MakePoint(_Center);
        _Result["radius"] = _Radius;
        _Result["diameter"] = 2.0 * _Radius;
        _Result["error"] = _MaximumError;
        return _Result;
    }

    ObjectMap MakeSectionObject(IN const SSectionSnapshot& Section_)
    {
        VariantArray _Loops;
        VariantArray _InnerLoops;
        for (const auto& _Loop : Section_.Loops)
        {
            ObjectMap _Item{
                { "id", _Loop.ID },
                { "inner", _Loop.bInner },
                { "pointCount", static_cast<unsigned long long>(_Loop.Points.size()) },
                { "area", std::abs(SignedArea(_Loop.Points)) }
            };
            _Loops.emplace_back(_Item);
            if (_Loop.bInner)
            {
                _InnerLoops.emplace_back(std::move(_Item));
            }
        }
        const auto _Width = Section_.Bounds.Empty
            ? 0.0
            : Section_.Bounds.Max.X - Section_.Bounds.Min.X;
        const auto _Height = Section_.Bounds.Empty
            ? 0.0
            : Section_.Bounds.Max.Y - Section_.Bounds.Min.Y;
        return {
            { "cavityCount", static_cast<unsigned long long>(Section_.nCavityCount) },
            { "loopCount", static_cast<unsigned long long>(Section_.Loops.size()) },
            { "area", Section_.dMaterialArea },
            { "centroid", MakePoint(Section_.Centroid) },
            { "principalLongAxis", MakeDirection(Section_.PrincipalLongAxis) },
            { "principalShortAxis", MakeDirection(Section_.PrincipalShortAxis) },
            { "bounds", ObjectMap{
                { "width", _Width },
                { "height", _Height },
                { "center", MakePoint({
                    0.5 * (Section_.Bounds.Min.X + Section_.Bounds.Max.X),
                    0.5 * (Section_.Bounds.Min.Y + Section_.Bounds.Max.Y) }) }
            } },
            { "loops", std::move(_Loops) },
            { "innerLoops", std::move(_InnerLoops) }
        };
    }

    std::optional<Variant> ResolvePath(
        IN const std::string& strPath_,
        IN const SRuleEvaluationContext& Context_)
    {
        std::string _Path = strPath_;
        if (!_Path.empty() && _Path.front() == '$')
        {
            _Path.erase(_Path.begin());
        }
        ObjectMap _Root = Context_.Variables;
        if (Context_.pSection)
        {
            _Root["section"] = MakeSectionObject(*Context_.pSection);
        }
        Variant _Current(_Root);
        std::size_t _Begin = 0;
        while (_Begin <= _Path.size())
        {
            const auto _End = _Path.find('.', _Begin);
            const auto _Name = _Path.substr(
                _Begin,
                _End == std::string::npos ? std::string::npos : _End - _Begin);
            if (!_Current.Is<ObjectMap>())
            {
                return std::nullopt;
            }
            const auto _Object = _Current.To<ObjectMap>();
            const auto _Iter = _Object.find(_Name);
            if (_Iter == _Object.end())
            {
                return std::nullopt;
            }
            _Current = _Iter->second;
            if (_End == std::string::npos)
            {
                return _Current;
            }
            _Begin = _End + 1;
        }
        return std::nullopt;
    }

    class CFunctionOperator final : public ISectionRuleOperator
    {
    public:
        using CFunction = std::function<Variant(
            const SRuleEvaluationContext&,
            const VariantArray&)>;

        CFunctionOperator(std::string strName_, CFunction Function_)
            : m_Name(std::move(strName_)), m_Function(std::move(Function_))
        {
        }

        std::string Name() const override
        {
            return m_Name;
        }

        Variant Evaluate(
            IN const SRuleEvaluationContext& Context_,
            IN const VariantArray& Arguments_) const override
        {
            return m_Function(Context_, Arguments_);
        }

    private:
        std::string m_Name;
        CFunction m_Function;
    };

    std::shared_ptr<ISectionRuleOperator> MakeOperator(
        IN std::string strName_,
        IN CFunctionOperator::CFunction Function_)
    {
        return std::make_shared<CFunctionOperator>(
            std::move(strName_),
            std::move(Function_));
    }

    bool IsKnownExpression(
        IN const Variant& Expression_,
        IN const CSectionRuleEngine::COperatorMap& Operators_,
        OUT std::vector<std::string>& Diagnostics_)
    {
        if (Expression_.Is<VariantArray>())
        {
            for (const auto& _Item : Expression_.To<VariantArray>())
            {
                if (!IsKnownExpression(_Item, Operators_, Diagnostics_))
                {
                    return false;
                }
            }
            return true;
        }
        if (!Expression_.Is<ObjectMap>())
        {
            return true;
        }
        const auto _Object = Expression_.To<ObjectMap>();
        const auto _Op = _Object.find("op");
        if (_Op != _Object.end())
        {
            const auto _Name = ToString(_Op->second);
            if (!_Name || !Operators_.contains(*_Name))
            {
                Diagnostics_.push_back(
                    "Unknown section rule operator: "
                    + (_Name ? *_Name : std::string("<invalid>")));
                return false;
            }
        }
        for (const auto& [_Name, _Value] : _Object)
        {
            if (_Name != "op" && !IsKnownExpression(
                _Value,
                Operators_,
                Diagnostics_))
            {
                return false;
            }
        }
        return true;
    }
}

CSectionRuleEngine::CSectionRuleEngine(IN const COperatorMap& Operators_)
    : m_Operators(Operators_)
{
}

Variant CSectionRuleEngine::Evaluate(
    IN const Variant& Expression_,
    IN OUT SRuleEvaluationContext& Context_) const
{
    if (Expression_.Is<VariantArray>())
    {
        VariantArray _Result;
        for (const auto& _Item : Expression_.To<VariantArray>())
        {
            _Result.emplace_back(Evaluate(_Item, Context_));
        }
        return _Result;
    }
    if (!Expression_.Is<ObjectMap>())
    {
        return Expression_;
    }

    const auto _Object = Expression_.To<ObjectMap>();
    if (const auto _Path = _Object.find("path"); _Path != _Object.end())
    {
        const auto _PathValue = ToString(_Path->second);
        if (!_PathValue)
        {
            throw std::invalid_argument("Section rule path must be a string");
        }
        const auto _Resolved = ResolvePath(*_PathValue, Context_);
        if (!_Resolved)
        {
            throw std::invalid_argument(
                "Section rule path does not exist: " + *_PathValue);
        }
        return *_Resolved;
    }
    if (const auto _Literal = _Object.find("value"); _Literal != _Object.end())
    {
        return _Literal->second;
    }
    if (const auto _Op = _Object.find("op"); _Op != _Object.end())
    {
        const auto _Name = ToString(_Op->second);
        if (!_Name)
        {
            throw std::invalid_argument("Section rule operator name must be a string");
        }
        const auto _Operator = m_Operators.find(*_Name);
        if (_Operator == m_Operators.end() || !_Operator->second)
        {
            throw std::invalid_argument("Unknown section rule operator: " + *_Name);
        }
        VariantArray _Arguments;
        if (const auto _Args = _Object.find("args"); _Args != _Object.end())
        {
            if (!_Args->second.Is<VariantArray>())
            {
                throw std::invalid_argument("Section rule args must be an array");
            }
            for (const auto& _Argument : _Args->second.To<VariantArray>())
            {
                _Arguments.emplace_back(Evaluate(_Argument, Context_));
            }
        }
        return _Operator->second->Evaluate(Context_, _Arguments);
    }

    ObjectMap _Result;
    for (const auto& [_Name, _Value] : _Object)
    {
        _Result[_Name] = Evaluate(_Value, Context_);
    }
    return _Result;
}

SDefinitionValidationResult CSectionRuleEngine::Validate(
    IN const SSectionTypeDefinition& Definition_) const
{
    SDefinitionValidationResult _Result;
    if (Definition_.TypeID.empty())
    {
        _Result.Diagnostics.push_back("Section type id cannot be empty");
    }
    if (Definition_.Match.Is<std::monostate>())
    {
        _Result.Diagnostics.push_back("Section type match expression is missing");
    }
    for (const auto& [_Name, _Expression] : Definition_.Bindings)
    {
        (void)_Name;
        IsKnownExpression(_Expression, m_Operators, _Result.Diagnostics);
    }
    IsKnownExpression(Definition_.Match, m_Operators, _Result.Diagnostics);
    for (const auto& [_Name, _Expression] : Definition_.Parameters)
    {
        (void)_Name;
        IsKnownExpression(_Expression, m_Operators, _Result.Diagnostics);
    }
    for (const auto& [_Name, _Expression] : Definition_.Placement)
    {
        (void)_Name;
        IsKnownExpression(_Expression, m_Operators, _Result.Diagnostics);
    }
    _Result.bValid = _Result.Diagnostics.empty();
    return _Result;
}

SSectionMatchResult CSectionRuleEngine::Match(
    IN const SSectionSnapshot& Section_,
    IN const SSectionTypeDefinition& Definition_,
    IN double dLinearTolerance_,
    IN double dAngularToleranceRadians_) const
{
    SSectionMatchResult _Result;
    const auto _Validation = Validate(Definition_);
    if (!_Validation.bValid)
    {
        _Result.Diagnostics = _Validation.Diagnostics;
        return _Result;
    }

    try
    {
        SRuleEvaluationContext _Context;
        _Context.pSection = &Section_;
        _Context.dLinearTolerance = dLinearTolerance_;
        _Context.dAngularToleranceRadians = dAngularToleranceRadians_;
        for (const auto& [_Name, _Expression] : Definition_.Bindings)
        {
            _Context.Variables[_Name] = Evaluate(_Expression, _Context);
        }
        const auto _Match = Evaluate(Definition_.Match, _Context);
        bool _bMatched = false;
        if (!ToBool(_Match, _bMatched) || !_bMatched)
        {
            return _Result;
        }

        for (const auto& [_Name, _Expression] : Definition_.Parameters)
        {
            _Result.Parameters[_Name] = Evaluate(_Expression, _Context);
        }

        _Result.Placement.Origin = Section_.Centroid;
        _Result.Placement.XDirection = Section_.PrincipalLongAxis;
        _Result.Placement.ZDirection = Section_.PrincipalShortAxis;
        if (const auto _Origin = Definition_.Placement.find("origin");
            _Origin != Definition_.Placement.end())
        {
            Point2 _Value;
            if (!TryReadPoint(Evaluate(_Origin->second, _Context), _Value))
            {
                _Result.Diagnostics.push_back("Placement origin must evaluate to point2");
                return _Result;
            }
            _Result.Placement.Origin = _Value;
        }
        if (const auto _X = Definition_.Placement.find("xDirection");
            _X != Definition_.Placement.end())
        {
            Direction2 _Value;
            if (!TryReadDirection(Evaluate(_X->second, _Context), _Value))
            {
                _Result.Diagnostics.push_back("Placement xDirection must evaluate to direction2");
                return _Result;
            }
            _Result.Placement.XDirection = _Value;
        }
        _Result.Placement.ZDirection = {
            -_Result.Placement.XDirection.Y,
            _Result.Placement.XDirection.X
        };
        if (const auto _Z = Definition_.Placement.find("zDirection");
            _Z != Definition_.Placement.end())
        {
            Direction2 _Value;
            if (!TryReadDirection(Evaluate(_Z->second, _Context), _Value))
            {
                _Result.Diagnostics.push_back("Placement zDirection must evaluate to direction2");
                return _Result;
            }
            const auto _Dot = std::abs(
                _Result.Placement.XDirection.X * _Value.X
                + _Result.Placement.XDirection.Y * _Value.Y);
            if (_Dot > std::max(1.0e-9, dAngularToleranceRadians_))
            {
                _Result.Diagnostics.push_back("Placement X/Z directions must be perpendicular");
                return _Result;
            }
            _Result.Placement.ZDirection = _Value;
        }
        _Result.bMatched = true;
        return _Result;
    }
    catch (const std::exception& Error_)
    {
        _Result.Diagnostics.push_back(Error_.what());
        return _Result;
    }
}

std::vector<std::shared_ptr<ISectionRuleOperator>>
CreateBuiltInSectionRuleOperators()
{
    std::vector<std::shared_ptr<ISectionRuleOperator>> _Operators;
    _Operators.push_back(MakeOperator("all", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        for (const auto& _Argument : Arguments_)
        {
            bool _Value = false;
            if (!ToBool(_Argument, _Value) || !_Value)
            {
                return false;
            }
        }
        return true;
    }));
    _Operators.push_back(MakeOperator("any", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        for (const auto& _Argument : Arguments_)
        {
            bool _Value = false;
            if (ToBool(_Argument, _Value) && _Value)
            {
                return true;
            }
        }
        return false;
    }));
    _Operators.push_back(MakeOperator("not", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        bool _Value = false;
        return Arguments_.size() == 1
            && ToBool(Arguments_.front(), _Value)
            && !_Value;
    }));
    _Operators.push_back(MakeOperator("equals", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        if (Arguments_.size() != 2)
        {
            return false;
        }
        const auto _Left = ToNumber(Arguments_[0]);
        const auto _Right = ToNumber(Arguments_[1]);
        if (_Left && _Right)
        {
            return std::abs(*_Left - *_Right) <= 1.0e-12;
        }
        return Arguments_[0] == Arguments_[1];
    }));
    _Operators.push_back(MakeOperator("greater", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        if (Arguments_.size() != 2)
        {
            return false;
        }
        const auto _Left = ToNumber(Arguments_[0]);
        const auto _Right = ToNumber(Arguments_[1]);
        return _Left && _Right && *_Left > *_Right;
    }));
    _Operators.push_back(MakeOperator("less", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        if (Arguments_.size() != 2)
        {
            return false;
        }
        const auto _Left = ToNumber(Arguments_[0]);
        const auto _Right = ToNumber(Arguments_[1]);
        return _Left && _Right && *_Left < *_Right;
    }));
    _Operators.push_back(MakeOperator("between", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        if (Arguments_.size() != 3)
        {
            return false;
        }
        const auto _Value = ToNumber(Arguments_[0]);
        const auto _Minimum = ToNumber(Arguments_[1]);
        const auto _Maximum = ToNumber(Arguments_[2]);
        return _Value && _Minimum && _Maximum
            && *_Value >= *_Minimum && *_Value <= *_Maximum;
    }));
    _Operators.push_back(MakeOperator("add", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        double _Result = 0.0;
        for (const auto& _Argument : Arguments_)
        {
            const auto _Value = ToNumber(_Argument);
            if (!_Value) return {};
            _Result += *_Value;
        }
        return _Result;
    }));
    _Operators.push_back(MakeOperator("subtract", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        if (Arguments_.size() != 2) return {};
        const auto _Left = ToNumber(Arguments_[0]);
        const auto _Right = ToNumber(Arguments_[1]);
        return _Left && _Right ? Variant(*_Left - *_Right) : Variant{};
    }));
    _Operators.push_back(MakeOperator("multiply", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        double _Result = 1.0;
        for (const auto& _Argument : Arguments_)
        {
            const auto _Value = ToNumber(_Argument);
            if (!_Value) return {};
            _Result *= *_Value;
        }
        return _Result;
    }));
    _Operators.push_back(MakeOperator("divide", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        if (Arguments_.size() != 2) return {};
        const auto _Left = ToNumber(Arguments_[0]);
        const auto _Right = ToNumber(Arguments_[1]);
        if (!_Left || !_Right || std::abs(*_Right) <= 1.0e-15) return {};
        return *_Left / *_Right;
    }));
    _Operators.push_back(MakeOperator("min", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        if (Arguments_.empty()) return {};
        auto _Result = (std::numeric_limits<double>::max)();
        for (const auto& _Argument : Arguments_)
        {
            const auto _Value = ToNumber(_Argument);
            if (!_Value) return {};
            _Result = std::min(_Result, *_Value);
        }
        return _Result;
    }));
    _Operators.push_back(MakeOperator("max", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        if (Arguments_.empty()) return {};
        auto _Result = (std::numeric_limits<double>::lowest)();
        for (const auto& _Argument : Arguments_)
        {
            const auto _Value = ToNumber(_Argument);
            if (!_Value) return {};
            _Result = std::max(_Result, *_Value);
        }
        return _Result;
    }));
    _Operators.push_back(MakeOperator("count", [](
        IN const SRuleEvaluationContext&,
        IN const VariantArray& Arguments_) -> Variant {
        if (Arguments_.size() != 1)
        {
            return static_cast<unsigned long long>(0);
        }
        if (Arguments_[0].Is<VariantArray>())
        {
            return static_cast<unsigned long long>(Arguments_[0].To<VariantArray>().size());
        }
        if (Arguments_[0].Is<ObjectMap>())
        {
            return static_cast<unsigned long long>(Arguments_[0].To<ObjectMap>().size());
        }
        if (Arguments_[0].Is<std::string>())
        {
            return static_cast<unsigned long long>(Arguments_[0].To<std::string>().size());
        }
        return static_cast<unsigned long long>(0);
    }));
    _Operators.push_back(MakeOperator("fit.rectangle", [](
        IN const SRuleEvaluationContext& Context_,
        IN const VariantArray& Arguments_) -> Variant {
        const auto _Selector = Arguments_.empty()
            ? std::optional<std::string>("outer")
            : ToString(Arguments_.front());
        if (!Context_.pSection || !_Selector)
        {
            return ObjectMap{ { "valid", false } };
        }
        return FitRectangle(
            FindLoop(*Context_.pSection, *_Selector),
            Context_.dLinearTolerance);
    }));
    _Operators.push_back(MakeOperator("fit.circle", [](
        IN const SRuleEvaluationContext& Context_,
        IN const VariantArray& Arguments_) -> Variant {
        const auto _Selector = Arguments_.empty()
            ? std::optional<std::string>("outer")
            : ToString(Arguments_.front());
        if (!Context_.pSection || !_Selector)
        {
            return ObjectMap{ { "valid", false } };
        }
        return FitCircle(
            FindLoop(*Context_.pSection, *_Selector),
            Context_.dLinearTolerance);
    }));
    _Operators.push_back(MakeOperator("section.centroid", [](
        IN const SRuleEvaluationContext& Context_,
        IN const VariantArray&) -> Variant {
        return Context_.pSection
            ? Variant(MakePoint(Context_.pSection->Centroid))
            : Variant(MakePoint({}));
    }));
    _Operators.push_back(MakeOperator("section.principalAxis", [](
        IN const SRuleEvaluationContext& Context_,
        IN const VariantArray& Arguments_) -> Variant {
        const auto _Kind = Arguments_.empty()
            ? std::optional<std::string>("long")
            : ToString(Arguments_.front());
        if (!Context_.pSection || !_Kind)
        {
            return MakeDirection({ 1.0, 0.0 });
        }
        return MakeDirection(*_Kind == "short"
            ? Context_.pSection->PrincipalShortAxis
            : Context_.pSection->PrincipalLongAxis);
    }));
    return _Operators;
}
}
