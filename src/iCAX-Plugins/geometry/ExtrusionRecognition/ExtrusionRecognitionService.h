#pragma once

#include "ExtrusionRecognitionTypes.h"
#include "SectionRuleEngine.h"

#include "Services/IService.h"
#include "Services/ServicesHelper.h"

#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>

namespace iCAX::ExtrusionRecognition
{
    class _EXTRUSION_RECOGNITION_EXP IExtrusionRecognitionService
        : public iCAX::Services::IService
    {
    public:
        ~IExtrusionRecognitionService() override = default;

        virtual SRecognitionResult Recognize(
            IN const iCAX::GeometryData::BRepModel& Geometry_,
            IN std::span<const SSectionTypeDefinition> OrderedDefinitions_,
            IN const SRecognitionOptions& Options_ = {}) = 0;

        virtual SSectionMatchResult TestSectionType(
            IN const SSectionSnapshot& Section_,
            IN const SSectionTypeDefinition& Definition_,
            IN double dLinearTolerance_ = 0.001,
            IN double dAngularToleranceRadians_ = 0.001) const = 0;

        virtual SDefinitionValidationResult ValidateDefinition(
            IN const SSectionTypeDefinition& Definition_) const = 0;

        /*
        * @brief 从标准 JSON 文本读取有序截面类型定义。
        * @details 仅解析内存文本，不访问文件、资源池或产品上下文。
        */
        virtual SDefinitionLoadResult ParseDefinitionsJSON(
            IN std::string_view JSON_) const = 0;

        virtual SDefinitionLoadResult DecodeDefinitions(
            IN const iCAX::Data::Variant& Document_) const = 0;

        virtual void RegisterOperator(
            IN std::shared_ptr<ISectionRuleOperator> pOperator_) = 0;
    };

    class _EXTRUSION_RECOGNITION_EXP CExtrusionRecognitionService final
        : public IExtrusionRecognitionService
    {
        AUTO_REGIST_SERVICE(
            iCAX::ExtrusionRecognition::IExtrusionRecognitionService,
            CExtrusionRecognitionService)

    public:
        CExtrusionRecognitionService() = default;
        ~CExtrusionRecognitionService() override = default;

        void OnLoad() override;
        void OnUnload() override;

        SRecognitionResult Recognize(
            IN const iCAX::GeometryData::BRepModel& Geometry_,
            IN std::span<const SSectionTypeDefinition> OrderedDefinitions_,
            IN const SRecognitionOptions& Options_ = {}) override;

        SSectionMatchResult TestSectionType(
            IN const SSectionSnapshot& Section_,
            IN const SSectionTypeDefinition& Definition_,
            IN double dLinearTolerance_ = 0.001,
            IN double dAngularToleranceRadians_ = 0.001) const override;

        SDefinitionValidationResult ValidateDefinition(
            IN const SSectionTypeDefinition& Definition_) const override;

        SDefinitionLoadResult ParseDefinitionsJSON(
            IN std::string_view JSON_) const override;

        SDefinitionLoadResult DecodeDefinitions(
            IN const iCAX::Data::Variant& Document_) const override;

        void RegisterOperator(
            IN std::shared_ptr<ISectionRuleOperator> pOperator_) override;

    private:
        mutable std::mutex m_Mutex;
        CSectionRuleEngine::COperatorMap m_Operators;
    };
}
