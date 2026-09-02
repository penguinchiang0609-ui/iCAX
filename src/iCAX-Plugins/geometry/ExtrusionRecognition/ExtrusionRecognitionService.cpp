#include "pch.h"
#include "ExtrusionRecognitionService.h"
#include "SectionDefinitionCodec.h"
#include "SectionGeometry.h"

namespace iCAX::ExtrusionRecognition
{
void CExtrusionRecognitionService::OnLoad()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Operators.clear();
    for (auto& _Operator : CreateBuiltInSectionRuleOperators())
    {
        if (_Operator)
        {
            m_Operators[_Operator->Name()] = std::move(_Operator);
        }
    }
}

void CExtrusionRecognitionService::OnUnload()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Operators.clear();
}

SRecognitionResult CExtrusionRecognitionService::Recognize(
    IN const iCAX::GeometryData::BRepModel& Geometry_,
    IN std::span<const SSectionTypeDefinition> OrderedDefinitions_,
    IN const SRecognitionOptions& Options_)
{
    SRecognitionResult _Result;
    if (OrderedDefinitions_.empty())
    {
        _Result.Status = ERecognitionStatus::InvalidRequest;
        _Result.Diagnostics.push_back(
            "Extrusion recognition requires ordered section type definitions");
        return _Result;
    }

    SExtrusionAnalysis _Analysis;
    if (!TryAnalyzeExtrusion(
        Geometry_,
        Options_,
        _Analysis,
        _Result.Status,
        _Result.Diagnostics))
    {
        return _Result;
    }

    CSectionRuleEngine::COperatorMap _Operators;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _Operators = m_Operators;
    }
    CSectionRuleEngine _RuleEngine(_Operators);
    for (const auto& _Definition : OrderedDefinitions_)
    {
        const auto _Validation = _RuleEngine.Validate(_Definition);
        if (!_Validation.bValid)
        {
            _Result.Status = ERecognitionStatus::SectionTypeDefinitionInvalid;
            _Result.Diagnostics.insert(
                _Result.Diagnostics.end(),
                _Validation.Diagnostics.begin(),
                _Validation.Diagnostics.end());
            return _Result;
        }

        auto _Match = _RuleEngine.Match(
            _Analysis.Section,
            _Definition,
            Options_.dLinearTolerance,
            Options_.dAngularToleranceRadians);
        if (!_Match.Diagnostics.empty())
        {
            _Result.Status = ERecognitionStatus::SectionTypeDefinitionInvalid;
            _Result.Diagnostics.insert(
                _Result.Diagnostics.end(),
                _Match.Diagnostics.begin(),
                _Match.Diagnostics.end());
            return _Result;
        }
        if (!_Match.bMatched)
        {
            continue;
        }

        std::string _NormalizeError;
        if (!TryNormalizeExtrusion(
            Geometry_,
            _Analysis,
            _Match.Placement,
            Options_.TargetAxis,
            _Result.NormalizedGeometry,
            _NormalizeError))
        {
            _Result.Status = ERecognitionStatus::PlacementFailed;
            _Result.Diagnostics.push_back(_NormalizeError);
            return _Result;
        }
        _Result.Status = ERecognitionStatus::Success;
        _Result.SectionTypeID = _Definition.TypeID;
        _Result.SectionParameters = std::move(_Match.Parameters);
        _Result.Section = std::move(_Analysis.Section);
        _Result.dLength = _Analysis.dLast - _Analysis.dFirst;
        return _Result;
    }

    _Result.Status = ERecognitionStatus::SectionTypeNotMatched;
    _Result.Section = std::move(_Analysis.Section);
    _Result.dLength = _Analysis.dLast - _Analysis.dFirst;
    _Result.Diagnostics.push_back(
        "No section type definition matched in the supplied order");
    return _Result;
}

SSectionMatchResult CExtrusionRecognitionService::TestSectionType(
    IN const SSectionSnapshot& Section_,
    IN const SSectionTypeDefinition& Definition_,
    IN double dLinearTolerance_,
    IN double dAngularToleranceRadians_) const
{
    CSectionRuleEngine::COperatorMap _Operators;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _Operators = m_Operators;
    }
    return CSectionRuleEngine(_Operators).Match(
        Section_,
        Definition_,
        dLinearTolerance_,
        dAngularToleranceRadians_);
}

SDefinitionValidationResult CExtrusionRecognitionService::ValidateDefinition(
    IN const SSectionTypeDefinition& Definition_) const
{
    CSectionRuleEngine::COperatorMap _Operators;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _Operators = m_Operators;
    }
    return CSectionRuleEngine(_Operators).Validate(Definition_);
}

SDefinitionLoadResult CExtrusionRecognitionService::ParseDefinitionsJSON(
    IN std::string_view JSON_) const
{
    auto _Result = DecodeSectionDefinitionsJSON(JSON_);
    if (!_Result.bValid)
    {
        return _Result;
    }

    CSectionRuleEngine::COperatorMap _Operators;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _Operators = m_Operators;
    }
    CSectionRuleEngine _RuleEngine(_Operators);
    std::unordered_set<std::string> _TypeIDs;
    for (std::size_t _Index = 0; _Index < _Result.Definitions.size(); ++_Index)
    {
        const auto& _Definition = _Result.Definitions[_Index];
        if (!_TypeIDs.insert(_Definition.TypeID).second)
        {
            _Result.Diagnostics.push_back(
                "Duplicate section type id at index " + std::to_string(_Index)
                + ": " + _Definition.TypeID);
            continue;
        }
        const auto _Validation = _RuleEngine.Validate(_Definition);
        for (const auto& _Diagnostic : _Validation.Diagnostics)
        {
            _Result.Diagnostics.push_back(
                "Section definition " + std::to_string(_Index)
                + " (" + _Definition.TypeID + "): " + _Diagnostic);
        }
    }
    _Result.bValid = _Result.Diagnostics.empty();
    if (!_Result.bValid)
    {
        _Result.Definitions.clear();
    }
    return _Result;
}

SDefinitionLoadResult CExtrusionRecognitionService::DecodeDefinitions(
    IN const iCAX::Data::Variant& Document_) const
{
    auto _Result = DecodeSectionDefinitions(Document_);
    if (!_Result.bValid)
    {
        return _Result;
    }

    CSectionRuleEngine::COperatorMap _Operators;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _Operators = m_Operators;
    }
    CSectionRuleEngine _RuleEngine(_Operators);
    std::unordered_set<std::string> _TypeIDs;
    for (std::size_t _Index = 0; _Index < _Result.Definitions.size(); ++_Index)
    {
        const auto& _Definition = _Result.Definitions[_Index];
        if (!_TypeIDs.insert(_Definition.TypeID).second)
        {
            _Result.Diagnostics.push_back(
                "Duplicate section type id at index " + std::to_string(_Index)
                + ": " + _Definition.TypeID);
            continue;
        }
        const auto _Validation = _RuleEngine.Validate(_Definition);
        for (const auto& _Diagnostic : _Validation.Diagnostics)
        {
            _Result.Diagnostics.push_back(
                "Section definition " + std::to_string(_Index)
                + " (" + _Definition.TypeID + "): " + _Diagnostic);
        }
    }
    _Result.bValid = _Result.Diagnostics.empty();
    if (!_Result.bValid)
    {
        _Result.Definitions.clear();
    }
    return _Result;
}

void CExtrusionRecognitionService::RegisterOperator(
    IN std::shared_ptr<ISectionRuleOperator> pOperator_)
{
    if (!pOperator_ || pOperator_->Name().empty())
    {
        throw std::invalid_argument(
            "Section rule operator and operator name are required");
    }
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Operators[pOperator_->Name()] = std::move(pOperator_);
}
}
