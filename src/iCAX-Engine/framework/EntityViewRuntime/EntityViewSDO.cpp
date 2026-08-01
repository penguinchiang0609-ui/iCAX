#include "pch.h"

#include "EntityViewSet.h"

#include "Data/VariantSerializer.h"
#include "DatabaseLanguage/EntityLanguage.h"
#include "ProjectContext/ISceneContext.h"
#include "SDO/SDO.h"
#include "SDO/SDORegistrationCatalog.h"

namespace
{
    iCAX::Data::ObjectMap DecodeObjectPayload(
        IN const iCAX::Interaction::CInvocation& Request_)
    {
        if (Request_.Payload.empty())
        {
            return {};
        }
        const std::string _Text(Request_.Payload.begin(), Request_.Payload.end());
        const auto _Value = iCAX::Data::VariantSerializer::Deserialize(_Text);
        if (!_Value.Is<iCAX::Data::ObjectMap>())
        {
            throw std::invalid_argument("EntityView payload must be an object");
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
            throw std::invalid_argument(
                "EntityView requires string field: " + strName_);
        }
        const auto _Value = _Iter->second.To<std::string>();
        if (_Value.empty())
        {
            throw std::invalid_argument(
                "EntityView field cannot be empty: " + strName_);
        }
        return _Value;
    }

    iCAX::Data::ObjectMap GetOptionalParameters(
        IN const iCAX::Data::ObjectMap& Payload_)
    {
        const auto _Iter = Payload_.find("parameters");
        if (_Iter == Payload_.end())
        {
            return {};
        }
        if (!_Iter->second.Is<iCAX::Data::ObjectMap>())
        {
            throw std::invalid_argument(
                "EntityView parameters must be an object");
        }
        return _Iter->second.To<iCAX::Data::ObjectMap>();
    }

    iCAX::Data::uuid GetRequiredViewID(
        IN const iCAX::Data::ObjectMap& Payload_)
    {
        const auto _Text = GetRequiredString(Payload_, "viewId");
        const auto _Parsed = iCAX::Data::uuid::from_string(_Text);
        if (!_Parsed.has_value() || _Parsed->is_nil())
        {
            throw std::invalid_argument(
                "EntityView viewId must be a non-nil uuid");
        }
        return *_Parsed;
    }

    iCAX::Interaction::CInvocationResult MakeObjectResponse(
        IN iCAX::Data::ObjectMap Payload_)
    {
        const auto _Text =
            iCAX::Data::VariantSerializer::Serialize(
                iCAX::Data::Variant(Payload_));
        iCAX::Interaction::CInvocationResult _Result;
        _Result.nStatus = iCAX::Interaction::EInvocationStatus::Ok;
        _Result.Payload.assign(_Text.begin(), _Text.end());
        return _Result;
    }

    iCAX::Interaction::CInvocationResult HandleGetOrCreate(
        IN const iCAX::Interaction::CInvocation& Request_,
        IN const iCAX::Application::IApplicationContext&,
        IN iCAX::Product::IProductContext*,
        IN iCAX::Project::IProjectContext*,
        IN iCAX::Project::ISceneContext* pSceneContext_)
    {
        if (!pSceneContext_)
        {
            throw std::invalid_argument(
                "EntityView.GetOrCreate requires scene scope");
        }

        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _LanguageIter = _Payload.find("language");
        const auto _Language = _LanguageIter == _Payload.end()
            ? std::string("sql")
            : GetRequiredString(_Payload, "language");
        const auto _WhereText = GetRequiredString(_Payload, "where");

        iCAX::Database::SEntityWhere _Where;
        if (_Language == "sql")
        {
            _Where = iCAX::DatabaseLanguage::CEntitySql::ParseWhere(_WhereText);
        }
        else if (_Language == "lambda")
        {
            _Where =
                iCAX::DatabaseLanguage::CEntityLambda::ParseWhere(_WhereText);
        }
        else
        {
            throw std::invalid_argument(
                "EntityView language must be sql or lambda");
        }

        const auto _Handle = pSceneContext_->EntityViews().GetOrCreate(
            _Where,
            GetOptionalParameters(_Payload));

        iCAX::Data::ObjectMap _PDO;
        _PDO["id"] = std::to_string(_Handle.nPDOID);
        _PDO["version"] =
            static_cast<unsigned int>(_Handle.nPDOLayoutVersion);
        _PDO["payloadSize"] =
            static_cast<unsigned long long>(_Handle.nPDOPayloadSize);
        _PDO["type"] = std::string("entity-view.membership");

        iCAX::Data::ObjectMap _Response;
        _Response["viewId"] = iCAX::Data::to_string(_Handle.ViewID);
        _Response["revision"] =
            static_cast<unsigned long long>(_Handle.nRevision);
        _Response["maxEntityCount"] =
            static_cast<unsigned int>(_Handle.nMaxEntityCount);
        _Response["pdo"] = std::move(_PDO);
        return MakeObjectResponse(std::move(_Response));
    }

    iCAX::Interaction::CInvocationResult HandleRelease(
        IN const iCAX::Interaction::CInvocation& Request_,
        IN const iCAX::Application::IApplicationContext&,
        IN iCAX::Product::IProductContext*,
        IN iCAX::Project::IProjectContext*,
        IN iCAX::Project::ISceneContext* pSceneContext_)
    {
        if (!pSceneContext_)
        {
            throw std::invalid_argument(
                "EntityView.Release requires scene scope");
        }

        const auto _Payload = DecodeObjectPayload(Request_);
        const auto _ViewID = GetRequiredViewID(_Payload);
        iCAX::Data::ObjectMap _Response;
        _Response["viewId"] = iCAX::Data::to_string(_ViewID);
        _Response["released"] =
            pSceneContext_->EntityViews().Release(_ViewID);
        return MakeObjectResponse(std::move(_Response));
    }

    class CEntityViewSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CEntityViewSDO()
            : CSDO("EntityView")
        {
            ExposeMethod("GetOrCreate", &HandleGetOrCreate);
            ExposeMethod("Release", &HandleRelease);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessSDOType<CEntityViewSDO>);
}

ICAX_REGISTER_SDO(CEntityViewSDO)
