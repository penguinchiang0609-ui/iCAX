#include "pch.h"

#include "IViewContentService.h"

#include "Data/VariantSerializer.h"
#include "Data/uuid.h"
#include "Facades/Facade.h"
#include "Facades/FacadeRegistrationCatalog.h"
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
            throw std::invalid_argument("View facade payload must be an object");
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
            throw std::invalid_argument("View facade requires string field: " + strName_);
        }
        const auto _Value = _Iter->second.To<std::string>();
        if (_Value.empty())
        {
            throw std::invalid_argument("View facade field cannot be empty: " + strName_);
        }
        return _Value;
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
            throw std::invalid_argument("View facade context must be an object");
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

        iCAX::Data::ObjectMap _Payload;
        _Payload["viewDefinitionId"] = Content_.DefinitionID;
        _Payload["revision"] = static_cast<unsigned long long>(Content_.nRevision);
        _Payload["objects"] = _Objects;

        const auto _Text = iCAX::Data::VariantSerializer::Serialize(iCAX::Data::Variant(_Payload));
        iCAX::Interaction::CInvocationResult _Result;
        _Result.nStatus = iCAX::Interaction::EInvocationStatus::Ok;
        _Result.Payload.assign(_Text.begin(), _Text.end());
        return _Result;
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
        const auto _DefinitionID = GetRequiredString(_Payload, "viewDefinitionId");
        const auto _Context = GetOptionalContext(_Payload);
        auto _pService = pSceneContext_->Services().Resolve<iCAX::View::IViewContentService>();
        return MakeResponse(_pService->GetContent(*pSceneContext_, _DefinitionID, _Context));
    }

    class CViewFacade final : public iCAX::Interaction::CFacade
    {
    public:
        CViewFacade()
            : CFacade("View")
        {
            ExposeMethod("GetContent", &HandleGetViewContent);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessFacadeType<CViewFacade>);
}

ICAX_REGISTER_FACADE(CViewFacade)
