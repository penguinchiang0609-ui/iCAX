#include "pch.h"

#include "IViewContentService.h"

#include "Data/VariantSerializer.h"
#include "Data/uuid.h"
#include "SDO/SDO.h"
#include "SDO/SDORegistrationCatalog.h"
#include "ProjectContext/ISceneContext.h"
#include "Services/ServiceProvider.h"

namespace
{
    iCAX::Data::ObjectMap DecodeObjectPayload(IN const iCAX::Interaction::CInvocation& Request_)
    {
        if (Request_.Payload.empty())
        {
            return {};
        }
        const std::string _Text(Request_.Payload.begin(), Request_.Payload.end());
        const auto _Value = iCAX::Data::VariantSerializer::Deserialize(_Text);
        if (!_Value.Is<iCAX::Data::ObjectMap>())
        {
            throw std::invalid_argument("View sdo payload must be an object");
        }
        return _Value.To<iCAX::Data::ObjectMap>();
    }

    std::string GetRequiredString(
        IN const iCAX::Data::ObjectMap& Payload_,
        IN const std::string& strName_)
    {
        const auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || !_Iter->second.Is<std::string>())
        {
            throw std::invalid_argument("View sdo requires string field: " + strName_);
        }
        const auto _Value = _Iter->second.To<std::string>();
        if (_Value.empty())
        {
            throw std::invalid_argument("View sdo field cannot be empty: " + strName_);
        }
        return _Value;
    }

    iCAX::Data::uuid GetRequiredUUID(
        IN const iCAX::Data::ObjectMap& Payload_,
        IN const std::string& strName_)
    {
        const auto _Text = GetRequiredString(Payload_, strName_);
        const auto _Parsed = iCAX::Data::uuid::from_string(_Text);
        if (!_Parsed.has_value() || _Parsed->is_nil())
        {
            throw std::invalid_argument("View sdo requires a valid uuid field: " + strName_);
        }
        return *_Parsed;
    }

    iCAX::Data::ObjectMap GetOptionalContext(IN const iCAX::Data::ObjectMap& Payload_)
    {
        const auto _Iter = Payload_.find("context");
        if (_Iter == Payload_.end())
        {
            return {};
        }
        if (!_Iter->second.Is<iCAX::Data::ObjectMap>())
        {
            throw std::invalid_argument("View sdo context must be an object");
        }
        return _Iter->second.To<iCAX::Data::ObjectMap>();
    }

    iCAX::Interaction::CInvocationResult MakeResponse(IN const iCAX::View::SViewContent& Content_)
    {
        iCAX::Data::VariantArray _Objects;
        _Objects.reserve(Content_.Objects.size());
        for (const auto& _Object : Content_.Objects)
        {
            iCAX::Data::ObjectMap _Item;
            _Item["entityId"] = iCAX::Data::to_string(_Object.EntityID);
            if (!_Object.Presentation.empty())
            {
                _Item["presentation"] = _Object.Presentation;
            }
            _Objects.emplace_back(_Item);
        }

        iCAX::Data::VariantArray _Outputs;
        _Outputs.reserve(Content_.Outputs.size());
        for (const auto& _Output : Content_.Outputs)
        {
            iCAX::Data::ObjectMap _Item;
            _Item["type"] = _Output.Type;
            _Item["properties"] = _Output.Properties;
            _Outputs.emplace_back(std::move(_Item));
        }

        iCAX::Data::ObjectMap _Payload;
        if (!Content_.InstanceID.is_nil())
        {
            _Payload["viewInstanceId"] = iCAX::Data::to_string(Content_.InstanceID);
        }
        _Payload["viewDefinitionId"] = Content_.DefinitionID;
        _Payload["revision"] = static_cast<unsigned long long>(Content_.nRevision);
        _Payload["objects"] = _Objects;
        _Payload["outputs"] = std::move(_Outputs);

        const auto _Text = iCAX::Data::VariantSerializer::Serialize(iCAX::Data::Variant(_Payload));
        iCAX::Interaction::CInvocationResult _Result;
        _Result.nStatus = iCAX::Interaction::EInvocationStatus::Ok;
        _Result.Payload.assign(_Text.begin(), _Text.end());
        return _Result;
    }

    iCAX::Interaction::CInvocationResult MakeObjectResponse(
        IN iCAX::Data::ObjectMap Payload_)
    {
        const auto _Text =
            iCAX::Data::VariantSerializer::Serialize(iCAX::Data::Variant(Payload_));
        iCAX::Interaction::CInvocationResult _Result;
        _Result.nStatus = iCAX::Interaction::EInvocationStatus::Ok;
        _Result.Payload.assign(_Text.begin(), _Text.end());
        return _Result;
    }

    iCAX::Interaction::CInvocationResult HandleOpenView(
        IN const iCAX::Interaction::CInvocation& Request_,
        IN const iCAX::Application::IApplicationContext&,
        IN iCAX::Product::IProductContext*,
        IN iCAX::Project::IProjectContext* pProjectContext_,
        IN iCAX::Project::ISceneContext* pSceneContext_)
    {
        if (!pProjectContext_ || !pSceneContext_)
        {
            throw std::invalid_argument("View.Open requires project and scene scope");
        }
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _DefinitionID = GetRequiredString(_Payload, "viewDefinitionId");
        const auto _Context = GetOptionalContext(_Payload);
        auto _pService = pSceneContext_->Services().Resolve<iCAX::View::IViewContentService>();
        return MakeResponse(_pService->OpenView(
            *pProjectContext_,
            *pSceneContext_,
            _DefinitionID,
            _Context));
    }

    iCAX::Interaction::CInvocationResult HandleGetViewContent(
        IN const iCAX::Interaction::CInvocation& Request_,
        IN const iCAX::Application::IApplicationContext&,
        IN iCAX::Product::IProductContext*,
        IN iCAX::Project::IProjectContext*,
        IN iCAX::Project::ISceneContext* pSceneContext_)
    {
        if (!pSceneContext_)
        {
            throw std::invalid_argument("View.GetContent requires a scene scope");
        }
        const auto _Payload = DecodeObjectPayload(Request_);
        auto _pService = pSceneContext_->Services().Resolve<iCAX::View::IViewContentService>();
        if (_Payload.find("viewInstanceId") != _Payload.end())
        {
            return MakeResponse(_pService->GetContent(
                *pSceneContext_,
                GetRequiredUUID(_Payload, "viewInstanceId")));
        }

        const auto _DefinitionID = GetRequiredString(_Payload, "viewDefinitionId");
        const auto _Context = GetOptionalContext(_Payload);
        return MakeResponse(_pService->GetContent(*pSceneContext_, _DefinitionID, _Context));
    }

    iCAX::Interaction::CInvocationResult HandleCloseView(
        IN const iCAX::Interaction::CInvocation& Request_,
        IN const iCAX::Application::IApplicationContext&,
        IN iCAX::Product::IProductContext*,
        IN iCAX::Project::IProjectContext* pProjectContext_,
        IN iCAX::Project::ISceneContext* pSceneContext_)
    {
        if (!pProjectContext_ || !pSceneContext_)
        {
            throw std::invalid_argument("View.Close requires project and scene scope");
        }
        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _InstanceID = GetRequiredUUID(_Payload, "viewInstanceId");
        auto _pService = pSceneContext_->Services().Resolve<iCAX::View::IViewContentService>();

        iCAX::Data::ObjectMap _Response;
        _Response["viewInstanceId"] = iCAX::Data::to_string(_InstanceID);
        _Response["closed"] = _pService->CloseView(
            *pProjectContext_,
            *pSceneContext_,
            _InstanceID);
        return MakeObjectResponse(std::move(_Response));
    }

    class CViewSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CViewSDO()
            : CSDO("View")
        {
            ExposeMethod("Open", &HandleOpenView);
            ExposeMethod("GetContent", &HandleGetViewContent);
            ExposeMethod("Close", &HandleCloseView);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessSDOType<CViewSDO>);
}

ICAX_REGISTER_SDO(CViewSDO)
