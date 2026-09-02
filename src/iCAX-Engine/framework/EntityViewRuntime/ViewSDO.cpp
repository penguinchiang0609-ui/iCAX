#include "pch.h"

#include "ViewSet.h"

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
            throw std::invalid_argument("View payload must be an object");
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
                "View requires string field: " + strName_);
        }
        const auto _Value = _Iter->second.To<std::string>();
        if (_Value.empty())
        {
            throw std::invalid_argument(
                "View field cannot be empty: " + strName_);
        }
        return _Value;
    }

    std::string GetOptionalString(
        IN const iCAX::Data::ObjectMap& Payload_,
        IN const std::string& strName_)
    {
        const auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end())
        {
            return {};
        }
        if (!_Iter->second.Is<std::string>())
        {
            throw std::invalid_argument(
                "View field must be string: " + strName_);
        }
        return _Iter->second.To<std::string>();
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
                "View source parameters must be an object");
        }
        return _Iter->second.To<iCAX::Data::ObjectMap>();
    }

    std::vector<iCAX::View::SViewProjectionField> GetProjection(
        IN const iCAX::Data::ObjectMap& Payload_)
    {
        const auto _Iter = Payload_.find("projection");
        if (_Iter == Payload_.end())
        {
            return {};
        }
        if (!_Iter->second.Is<iCAX::Data::VariantArray>())
        {
            throw std::invalid_argument(
                "View source projection must be an array");
        }

        std::vector<iCAX::View::SViewProjectionField> _Projection;
        for (const auto& _Item : _Iter->second.To<iCAX::Data::VariantArray>())
        {
            if (!_Item.Is<iCAX::Data::ObjectMap>())
            {
                throw std::invalid_argument(
                    "Each View projection field must be an object");
            }
            const auto _Field = _Item.To<iCAX::Data::ObjectMap>();
            iCAX::View::SViewProjectionField _ProjectionField;
            _ProjectionField.Alias = GetRequiredString(_Field, "alias");
            _ProjectionField.ComponentClass =
                GetRequiredString(_Field, "component");
            _ProjectionField.PropertyName =
                GetRequiredString(_Field, "property");
            const auto _Resource = _Field.find("resourceReference");
            if (_Resource != _Field.end())
            {
                if (!_Resource->second.Is<bool>())
                {
                    throw std::invalid_argument(
                        "View projection resourceReference must be boolean");
                }
                _ProjectionField.bResourceReference =
                    _Resource->second.To<bool>();
            }
            _Projection.push_back(std::move(_ProjectionField));
        }
        return _Projection;
    }

    iCAX::Database::SEntityWhere ParseWhere(
        IN const iCAX::Data::ObjectMap& Source_)
    {
        const auto _Language = [&]()
        {
            const auto _Iter = Source_.find("language");
            return _Iter == Source_.end()
                ? std::string("sql")
                : GetRequiredString(Source_, "language");
        }();
        const auto _Where = GetRequiredString(Source_, "where");
        if (_Language == "sql")
        {
            return iCAX::DatabaseLanguage::CEntitySql::ParseWhere(_Where);
        }
        if (_Language == "lambda")
        {
            return iCAX::DatabaseLanguage::CEntityLambda::ParseWhere(_Where);
        }
        throw std::invalid_argument(
            "View source language must be sql or lambda");
    }

    iCAX::View::SViewDefinition GetDefinition(
        IN const iCAX::Data::ObjectMap& Payload_)
    {
        const auto _Iter = Payload_.find("sources");
        if (_Iter == Payload_.end()
            || !_Iter->second.Is<iCAX::Data::VariantArray>())
        {
            throw std::invalid_argument("View sources must be an array");
        }

        iCAX::View::SViewDefinition _Definition;
        for (const auto& _Item : _Iter->second.To<iCAX::Data::VariantArray>())
        {
            if (!_Item.Is<iCAX::Data::ObjectMap>())
            {
                throw std::invalid_argument(
                    "Each View source must be an object");
            }
            const auto _SourcePayload = _Item.To<iCAX::Data::ObjectMap>();
            iCAX::View::SViewSourceDefinition _Source;
            _Source.SourceID = GetRequiredString(_SourcePayload, "sourceId");
            _Source.Role = GetOptionalString(_SourcePayload, "role");
            _Source.Where = ParseWhere(_SourcePayload);
            _Source.Parameters = GetOptionalParameters(_SourcePayload);
            _Source.Projection = GetProjection(_SourcePayload);
            _Definition.Sources.push_back(std::move(_Source));
        }
        return _Definition;
    }

    iCAX::Data::uuid GetRequiredViewID(
        IN const iCAX::Data::ObjectMap& Payload_)
    {
        const auto _Text = GetRequiredString(Payload_, "viewId");
        const auto _Parsed = iCAX::Data::uuid::from_string(_Text);
        if (!_Parsed.has_value() || _Parsed->is_nil())
        {
            throw std::invalid_argument(
                "View viewId must be a non-nil uuid");
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
                "View.GetOrCreate requires scene scope");
        }
        const auto _Handle = pSceneContext_->Views().GetOrCreate(
            GetDefinition(DecodeObjectPayload(Request_)));

        iCAX::Data::ObjectMap _Resource;
        _Resource["url"] = _Handle.Snapshot.URL;
        _Resource["version"] =
            static_cast<unsigned long long>(_Handle.Snapshot.nVersion);
        _Resource["format"] = _Handle.Format;
        _Resource["mediaType"] =
            std::string("application/vnd.icax.flatbuffer");

        iCAX::Data::ObjectMap _Response;
        _Response["viewId"] = iCAX::Data::to_string(_Handle.ID);
        _Response["revision"] =
            static_cast<unsigned long long>(_Handle.nRevision);
        _Response["resource"] = std::move(_Resource);
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
            throw std::invalid_argument("View.Release requires scene scope");
        }
        const auto _ViewID =
            GetRequiredViewID(DecodeObjectPayload(Request_));
        iCAX::Data::ObjectMap _Response;
        _Response["viewId"] = iCAX::Data::to_string(_ViewID);
        _Response["released"] =
            pSceneContext_->Views().Release(_ViewID);
        return MakeObjectResponse(std::move(_Response));
    }

    class CViewSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CViewSDO()
            : CSDO("View")
        {
            ExposeMethod("GetOrCreate", &HandleGetOrCreate);
            ExposeMethod("Release", &HandleRelease);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessSDOType<CViewSDO>);
}

ICAX_REGISTER_SDO(CViewSDO)
