#pragma once

#include "FacadeSupport.h"
#include "MachineResources.h"

#include "ApplicationContext/IApplicationContext.h"
#include "ProductContext/IProductContext.h"

namespace iCAX::CAM::Facades::Internal
{
constexpr const char* kMachineDefinitionsSettingKey = "machineDefinitions";
constexpr const char* kDefaultMachineDefinitionSettingKey = "defaultMachineDefinitionId";

std::string _MakeProductMachineDefinitionManagedPath(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_,
    IN const std::string& strSourcePath_);

iCAX::Data::ObjectMap _MakeProductMachineDefinition(
    IN const std::string& strDefinitionID_,
    IN const std::string& strName_,
    IN const std::string& strSourcePath_,
    IN const std::string& strManagedPath_,
    IN const bool bEnabled_,
    IN const bool bDefault_);

iCAX::Data::VariantArray _GetProductMachineDefinitionArray(
    IN iCAX::Product::IProductContext& ProductContext_);

iCAX::Data::VariantArray _GetProductMachineDefinitionSupportedFormats(
    IN const iCAX::Product::IProductContext& ProductContext_);

bool _IsSupportedProductMachineDefinitionPath(
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strSourcePath_);

iCAX::Data::ObjectMap _FindProductMachineDefinition(
    IN iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_);

iCAX::Data::VariantArray _UpsertProductMachineDefinition(
    IN iCAX::Product::IProductContext& ProductContext_,
    IN const iCAX::Data::ObjectMap& Definition_);

iCAX::Data::VariantArray _SetProductMachineDefinitionEnabled(
    IN iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_,
    IN bool bEnabled_);

iCAX::Data::VariantArray _DeleteProductMachineDefinition(
    IN iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_);

iCAX::Data::VariantArray _SetDefaultProductMachineDefinition(
    IN iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_);

std::string _EnsureProductMachineDefinitionFiles(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_,
    IN const std::string& strSourcePath_);

void _EnsureBuiltInProductMachineDefinitions(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext& ProductContext_);
} // namespace iCAX::CAM::Facades::Internal
