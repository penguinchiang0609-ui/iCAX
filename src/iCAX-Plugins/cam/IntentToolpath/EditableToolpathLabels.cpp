#include "pch.h"
#include "EditableToolpathLabels.h"
#include "EditableToolpathInternal.h"
#include "Database/IRepository.h"
#include <charconv>
#include <limits>

namespace iCAX::CAM::Intent
{
using namespace Editable;
namespace
{
using LabelNumber=std::uint64_t;
LabelNumber PositiveNumber(const Variant& value)
{
    LabelNumber number=0;
    if(value.Is<unsigned long long>())number=value.To<unsigned long long>();
    else if(value.Is<unsigned int>())number=value.To<unsigned int>();
    else if(value.Is<int>()){const auto signedValue=value.To<int>();Require(signedValue>0,"TOOLPATH_LABEL_FIRST_NUMBER_INVALID");number=static_cast<LabelNumber>(signedValue);}
    else if(value.Is<long long>()){const auto signedValue=value.To<long long>();Require(signedValue>0,"TOOLPATH_LABEL_FIRST_NUMBER_INVALID");number=static_cast<LabelNumber>(signedValue);}
    else if(value.Is<double>())
    {const auto real=value.To<double>();Require(std::isfinite(real)&&real>=1&&real<=9007199254740991.&&std::floor(real)==real,"TOOLPATH_LABEL_FIRST_NUMBER_INVALID");number=static_cast<LabelNumber>(real);}
    else throw std::invalid_argument("TOOLPATH_LABEL_FIRST_NUMBER_INVALID");
    Require(number>=1,"TOOLPATH_LABEL_FIRST_NUMBER_INVALID");return number;
}
std::optional<LabelNumber> NumericLabel(const std::string& text)
{
    if(text.empty())return {};
    for(const auto character:text)if(character<'0'||character>'9')return {};
    LabelNumber value=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    Require(result.ec!=std::errc::result_out_of_range,"TOOLPATH_LABEL_NUMBER_OVERFLOW");
    Require(result.ec==std::errc{}&&result.ptr==text.data()+text.size(),"TOOLPATH_LABEL_NUMBER_INVALID");return value;
}
}
void ValidateToolpathLabelScope(const std::string& scope)
{Require(!scope.empty(),"TOOLPATH_LABEL_SCOPE_REQUIRED");}
void ValidateToolpathOutputLabelPolicy(const IntentMap& policy)
{
    if(policy.empty())return;
    for(const auto& [key,value]:policy)Require(key=="numberScope"||key=="firstNumber"||key=="name","TOOLPATH_LABEL_POLICY_FIELD_UNSUPPORTED: "+key);
    Require(At(policy,"numberScope").Is<std::string>(),"TOOLPATH_LABEL_SCOPE_REQUIRED");ValidateToolpathLabelScope(Text(policy,"numberScope"));
    (void)PositiveNumber(At(policy,"firstNumber"));
    if(policy.contains("name"))Require(At(policy,"name").Is<std::string>(),"TOOLPATH_LABEL_NAME_INVALID");
}
std::map<IntentId,IntentMap> AllocateInitialToolpathLabels(const iCAX::Database::IRepository& repository,
    IntentId documentId,IntentId originOperationId,const IntentMap& policy,const std::vector<IntentId>& outputPathIds)
{
    ValidateToolpathOutputLabelPolicy(policy);Require(!documentId.is_nil()&&!originOperationId.is_nil(),"TOOLPATH_LABEL_OWNER_REQUIRED");
    Require(!repository.IsWriteInProgress(),"TOOLPATH_LABEL_ALLOCATION_DURING_TRANSACTION");
    std::set<IntentId> unique,newPaths;
    for(auto path:outputPathIds)
    {
        Require(!path.is_nil()&&unique.insert(path).second,"TOOLPATH_OUTPUT_IDENTITY_DUPLICATE");
        const auto entity=repository.GetEntity(path);if(!entity){newPaths.insert(path);continue;}
        const auto identity=std::dynamic_pointer_cast<CEditableDerivedComponent>(entity->GetComponent(CEditableDerivedComponent::S_ClassName));
        Require(identity&&identity->GetOwner()==documentId&&Text(identity->GetData(),"kind")=="pathIdentity","TOOLPATH_PATH_ID_COLLISION");
    }
    std::map<IntentId,IntentMap> result;if(newPaths.empty())return result;
    LabelNumber next=0;std::string scope,name;
    if(!policy.empty())
    {
        scope=Text(policy,"numberScope");name=Text(policy,"name");next=PositiveNumber(At(policy,"firstNumber"));LabelNumber maximum=0;
        const auto reserve=[&](const std::string& text){if(const auto number=NumericLabel(text))maximum=std::max(maximum,*number);};
        for(const auto& entity:repository.FilterEntities([](const auto&){return true;}))
        {
            const auto identity=std::dynamic_pointer_cast<CEditableDerivedComponent>(entity->GetComponent(CEditableDerivedComponent::S_ClassName));
            if(!identity||identity->GetOwner()!=documentId)continue;const auto data=identity->GetData();
            if(Text(data,"kind")!="pathIdentity"||Text(data,"labelScope")!=scope)continue;
            reserve(Text(data,"initialUserNumber"));
            if(const auto labels=std::dynamic_pointer_cast<CEditablePathLabelsComponent>(entity->GetComponent(CEditablePathLabelsComponent::S_ClassName)))reserve(labels->GetUserNumber());
        }
        if(maximum>=next){Require(maximum<std::numeric_limits<LabelNumber>::max(),"TOOLPATH_LABEL_NUMBER_OVERFLOW");next=maximum+1;}
        Require(newPaths.size()-1<=std::numeric_limits<LabelNumber>::max()-next,"TOOLPATH_LABEL_NUMBER_OVERFLOW");
    }
    for(auto path:newPaths)
    {
        Map data{{"kind",std::string("pathIdentity")},{"originOperation",originOperationId}};
        if(!policy.empty())
        {const auto number=std::to_string(next);data["labelScope"]=scope;data["initialUserNumber"]=number;data["initialName"]=name.empty()?std::string("刀路 ")+number:name;if(path!=*newPaths.rbegin())++next;}
        result.emplace(path,std::move(data));
    }
    return result;
}
}
