#pragma once

#include "ExtrusionRecognitionTypes.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace iCAX::ExtrusionRecognition
{
    struct _EXTRUSION_RECOGNITION_EXP SRuleEvaluationContext final
    {
        const SSectionSnapshot* pSection = nullptr;
        iCAX::Data::ObjectMap Variables;
        double dLinearTolerance = 0.001;
        double dAngularToleranceRadians = 0.001;
    };

    /*
    * @brief 声明式截面规则的一个通用 C++ 算子。
    * @details 扩展点位于几何能力而不是具体管型；一个新算子可被任意截面类型声明复用。
    */
    class _EXTRUSION_RECOGNITION_EXP ISectionRuleOperator
    {
    public:
        virtual ~ISectionRuleOperator() = default;

        virtual std::string Name() const = 0;

        virtual iCAX::Data::Variant Evaluate(
            IN const SRuleEvaluationContext& Context_,
            IN const iCAX::Data::VariantArray& Arguments_) const = 0;
    };

    class CSectionRuleEngine final
    {
    public:
        using COperatorMap = std::unordered_map<
            std::string,
            std::shared_ptr<ISectionRuleOperator>>;

        explicit CSectionRuleEngine(IN const COperatorMap& Operators_);

        iCAX::Data::Variant Evaluate(
            IN const iCAX::Data::Variant& Expression_,
            IN OUT SRuleEvaluationContext& Context_) const;

        SDefinitionValidationResult Validate(
            IN const SSectionTypeDefinition& Definition_) const;

        SSectionMatchResult Match(
            IN const SSectionSnapshot& Section_,
            IN const SSectionTypeDefinition& Definition_,
            IN double dLinearTolerance_,
            IN double dAngularToleranceRadians_) const;

    private:
        const COperatorMap& m_Operators;
    };

    std::vector<std::shared_ptr<ISectionRuleOperator>>
        CreateBuiltInSectionRuleOperators();
}

