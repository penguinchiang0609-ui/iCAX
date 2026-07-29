#include "pch.h"
#include "ResourceAccess.h"

#include "ResourceInfo.h"
#include "ResourceLibrary.h"
#include "ResourcePool.h"

#include <flatbuffers/flatbuffers.h>

#include <charconv>
#include <limits>
#include <sstream>

namespace
{
    using namespace iCAX::Resource;

    inline constexpr const char* kFlatBufferMediaType =
        "application/vnd.icax.flatbuffer";
    inline constexpr const char* kAllowedMethods =
        "HEAD, GET, POST, PUT, DELETE, OPTIONS";

    std::string Trim(IN const std::string& strValue_)
    {
        auto _Begin = strValue_.begin();
        while (_Begin != strValue_.end() &&
            std::isspace(static_cast<unsigned char>(*_Begin)))
        {
            ++_Begin;
        }

        auto _End = strValue_.end();
        while (_End != _Begin &&
            std::isspace(static_cast<unsigned char>(*std::prev(_End))))
        {
            --_End;
        }
        return std::string(_Begin, _End);
    }

    std::string ToLowerASCII(IN const std::string& strValue_)
    {
        auto _Result = strValue_;
        std::transform(
            _Result.begin(),
            _Result.end(),
            _Result.begin(),
            [](const unsigned char Value_) {
                return static_cast<char>(std::tolower(Value_));
            });
        return _Result;
    }

    bool HeaderNameEquals(
        IN const std::string& strLeft_,
        IN const std::string& strRight_)
    {
        return ToLowerASCII(strLeft_) == ToLowerASCII(strRight_);
    }

    CResourceResponse MakeResponse(IN const uint16_t nStatus_)
    {
        CResourceResponse _Response;
        _Response.nStatus = nStatus_;
        return _Response;
    }

    std::string MakeETag(IN const uint64_t nVersion_)
    {
        return "\"icax-v" + std::to_string(nVersion_) + "\"";
    }

    bool ETagListMatches(
        IN const std::string& strValues_,
        IN const std::string& strCurrentETag_)
    {
        std::size_t _Begin = 0;
        while (_Begin <= strValues_.size())
        {
            const auto _End = strValues_.find(',', _Begin);
            const auto _Value = Trim(strValues_.substr(
                _Begin,
                _End == std::string::npos
                    ? std::string::npos
                    : _End - _Begin));
            if (_Value == "*" || _Value == strCurrentETag_)
            {
                return true;
            }
            if (_End == std::string::npos)
            {
                break;
            }
            _Begin = _End + 1;
        }
        return false;
    }

    bool AcceptsMediaType(
        IN const std::optional<std::string>& strAccept_,
        IN const std::string& strMediaType_)
    {
        if (!strAccept_ || strAccept_->empty())
        {
            return true;
        }

        auto _MediaType = ToLowerASCII(Trim(strMediaType_));
        if (const auto _Parameter = _MediaType.find(';');
            _Parameter != std::string::npos)
        {
            _MediaType.erase(_Parameter);
            _MediaType = Trim(_MediaType);
        }
        std::size_t _Begin = 0;
        while (_Begin <= strAccept_->size())
        {
            const auto _End = strAccept_->find(',', _Begin);
            auto _Value = Trim(strAccept_->substr(
                _Begin,
                _End == std::string::npos
                    ? std::string::npos
                    : _End - _Begin));
            if (const auto _Parameter = _Value.find(';');
                _Parameter != std::string::npos)
            {
                _Value.erase(_Parameter);
                _Value = Trim(_Value);
            }
            _Value = ToLowerASCII(_Value);
            if (_Value == "*/*" || _Value == _MediaType)
            {
                return true;
            }

            const auto _Slash = _MediaType.find('/');
            if (_Slash != std::string::npos &&
                _Value == _MediaType.substr(0, _Slash) + "/*")
            {
                return true;
            }

            if (_End == std::string::npos)
            {
                break;
            }
            _Begin = _End + 1;
        }
        return false;
    }

    std::optional<uint32_t> ParseOptionalUInt32Header(
        IN const CResourceHeaders& Headers_,
        IN const std::string& strName_)
    {
        const auto _Value = GetResourceHeader(Headers_, strName_);
        if (!_Value || _Value->empty())
        {
            return std::nullopt;
        }

        uint32_t _Result = 0;
        const auto* _Begin = _Value->data();
        const auto* _End = _Begin + _Value->size();
        const auto _Parsed = std::from_chars(_Begin, _End, _Result);
        if (_Parsed.ec != std::errc() || _Parsed.ptr != _End)
        {
            throw std::invalid_argument(strName_ + " must be an unsigned integer");
        }
        return _Result;
    }

    std::optional<uint64_t> ParseOptionalUInt64Header(
        IN const CResourceHeaders& Headers_,
        IN const std::string& strName_)
    {
        const auto _Value =
            GetResourceHeader(Headers_, strName_);
        if (!_Value || _Value->empty())
        {
            return std::nullopt;
        }

        uint64_t _Result = 0;
        const auto* _Begin = _Value->data();
        const auto* _End = _Begin + _Value->size();
        const auto _Parsed =
            std::from_chars(_Begin, _End, _Result);
        if (_Parsed.ec != std::errc() ||
            _Parsed.ptr != _End)
        {
            throw std::invalid_argument(
                strName_ +
                " must be an unsigned integer");
        }
        return _Result;
    }

    std::string GetMediaType(
        IN const CResourceInfo& Info_,
        IN const std::optional<std::type_index>& RuntimeType_ = std::nullopt)
    {
        if (!Info_.MediaType.empty())
        {
            return Info_.MediaType;
        }
        if ((RuntimeType_ &&
            *RuntimeType_ == std::type_index(typeid(CFlatBufferResource))) ||
            Info_.ResourceTypeID == CFlatBufferResource::kResourceTypeName)
        {
            return kFlatBufferMediaType;
        }
        return "application/octet-stream";
    }

    void AppendRepresentationHeaders(
        IN OUT CResourceResponse& Response_,
        IN const CResourceInfo& Info_,
        IN const uint64_t nContentLength_,
        IN const std::string& strMediaType_)
    {
        SetResourceHeader(
            Response_.Headers,
            "Content-Type",
            strMediaType_);
        SetResourceHeader(
            Response_.Headers,
            "Content-Length",
            std::to_string(nContentLength_));
        SetResourceHeader(
            Response_.Headers,
            "ICAX-Resource-Length",
            std::to_string(nContentLength_));
        SetResourceHeader(
            Response_.Headers,
            "ETag",
            MakeETag(Info_.nVersion));
        SetResourceHeader(
            Response_.Headers,
            "ICAX-Resource-Version",
            std::to_string(Info_.nVersion));
        if (!Info_.ResourceID.is_nil())
        {
            SetResourceHeader(
                Response_.Headers,
                "ICAX-Resource-ID",
                iCAX::Data::to_string(Info_.ResourceID));
        }

        if (!Info_.ResourceTypeID.empty())
        {
            SetResourceHeader(
                Response_.Headers,
                "ICAX-Resource-Type",
                Info_.ResourceTypeID);
        }
        if (Info_.nSchemaVersion != 0)
        {
            SetResourceHeader(
                Response_.Headers,
                "ICAX-Schema-Version",
                std::to_string(Info_.nSchemaVersion));
        }
        if (Info_.nMinimumReaderVersion != 0)
        {
            SetResourceHeader(
                Response_.Headers,
                "ICAX-Min-Reader-Version",
                std::to_string(Info_.nMinimumReaderVersion));
        }
        if (!Info_.FlatBufferIdentifier.empty())
        {
            SetResourceHeader(
                Response_.Headers,
                "ICAX-FlatBuffer-Identifier",
                Info_.FlatBufferIdentifier);
        }
        if (!Info_.ContentHash.empty())
        {
            SetResourceHeader(
                Response_.Headers,
                "ICAX-Content-Hash",
                Info_.ContentHash);
        }
    }

    bool IsFlatBufferMediaType(IN const std::string& strMediaType_)
    {
        auto _MediaType = ToLowerASCII(Trim(strMediaType_));
        if (const auto _Parameter = _MediaType.find(';');
            _Parameter != std::string::npos)
        {
            _MediaType.erase(_Parameter);
            _MediaType = Trim(_MediaType);
        }
        return _MediaType == kFlatBufferMediaType ||
            _MediaType.ends_with("+flatbuffers") ||
            _MediaType.ends_with("+flatbuffer");
    }

    bool HasPlausibleFlatBufferRoot(IN const CFlatBufferResource& Body_)
    {
        if (Body_.Size() < sizeof(flatbuffers::uoffset_t))
        {
            return false;
        }

        const auto _RootOffset =
            flatbuffers::ReadScalar<flatbuffers::uoffset_t>(Body_.Data());
        return _RootOffset >= sizeof(flatbuffers::uoffset_t) &&
            _RootOffset < Body_.Size();
    }

    EResourceVersionCondition ResolveWriteCondition(
        IN const CResourceRequest& Request_,
        IN const std::optional<CResourceInfo>& CurrentInfo_,
        OUT uint64_t& nExpectedVersion_,
        OUT bool& bPreconditionFailed_)
    {
        nExpectedVersion_ = 0;
        bPreconditionFailed_ = false;

        const auto _IfMatch = GetResourceHeader(Request_.Headers, "If-Match");
        const auto _IfNoneMatch = GetResourceHeader(Request_.Headers, "If-None-Match");
        if (_IfMatch && _IfNoneMatch)
        {
            throw std::invalid_argument(
                "If-Match and If-None-Match cannot be used together");
        }

        if (_IfNoneMatch)
        {
            if (Trim(*_IfNoneMatch) != "*")
            {
                throw std::invalid_argument(
                    "Resource writes currently support only If-None-Match: *");
            }
            return EResourceVersionCondition::MustNotExist;
        }

        if (!_IfMatch)
        {
            return EResourceVersionCondition::None;
        }

        if (Trim(*_IfMatch) == "*")
        {
            return EResourceVersionCondition::MustExist;
        }

        if (!CurrentInfo_)
        {
            bPreconditionFailed_ = true;
            return EResourceVersionCondition::None;
        }

        const auto _CurrentETag = MakeETag(CurrentInfo_->nVersion);
        if (!ETagListMatches(*_IfMatch, _CurrentETag))
        {
            bPreconditionFailed_ = true;
            return EResourceVersionCondition::None;
        }

        nExpectedVersion_ = CurrentInfo_->nVersion;
        return EResourceVersionCondition::VersionMatches;
    }
}

iCAX::Resource::EResourceMethod
iCAX::Resource::ParseResourceMethod(IN const std::string& strMethod_)
{
    const auto _Method = ToLowerASCII(Trim(strMethod_));
    if (_Method == "head")
    {
        return EResourceMethod::Head;
    }
    if (_Method == "get")
    {
        return EResourceMethod::Get;
    }
    if (_Method == "post")
    {
        return EResourceMethod::Post;
    }
    if (_Method == "put")
    {
        return EResourceMethod::Put;
    }
    if (_Method == "delete")
    {
        return EResourceMethod::Delete;
    }
    if (_Method == "options")
    {
        return EResourceMethod::Options;
    }
    throw std::invalid_argument("Unsupported resource method: " + strMethod_);
}

const char*
iCAX::Resource::ToString(IN const EResourceMethod Method_) noexcept
{
    switch (Method_)
    {
    case EResourceMethod::Head:
        return "HEAD";
    case EResourceMethod::Get:
        return "GET";
    case EResourceMethod::Post:
        return "POST";
    case EResourceMethod::Put:
        return "PUT";
    case EResourceMethod::Delete:
        return "DELETE";
    case EResourceMethod::Options:
        return "OPTIONS";
    default:
        return "";
    }
}

std::optional<std::string>
iCAX::Resource::GetResourceHeader(
    IN const CResourceHeaders& Headers_,
    IN const std::string& strName_)
{
    for (const auto& _Header : Headers_)
    {
        if (HeaderNameEquals(_Header.first, strName_))
        {
            return _Header.second;
        }
    }
    return std::nullopt;
}

void iCAX::Resource::SetResourceHeader(
    IN OUT CResourceHeaders& Headers_,
    IN const std::string& strName_,
    IN std::string strValue_)
{
    for (auto _Iter = Headers_.begin(); _Iter != Headers_.end();)
    {
        if (HeaderNameEquals(_Iter->first, strName_))
        {
            _Iter = Headers_.erase(_Iter);
        }
        else
        {
            ++_Iter;
        }
    }
    Headers_.emplace(strName_, std::move(strValue_));
}

iCAX::Resource::CResourceAccessService::CResourceAccessService(
    IN CResourceLibrary& Library_) noexcept
    : m_Library(Library_)
{
}

iCAX::Resource::CResourceResponse
iCAX::Resource::CResourceAccessService::Request(
    IN const CResourceRequest& Request_)
{
    if (Request_.URL.empty())
    {
        return MakeResponse(400);
    }

    try
    {
        const auto _URL = ParseResourceURL(Request_.URL);
        if (m_Library.HasScope() &&
            !ResourceScopeEquals(
                m_Library.GetScope(),
                _URL.Owner))
        {
            return MakeResponse(404);
        }
        if (Request_.Method == EResourceMethod::Post)
        {
            if (!_URL.IsCollection())
            {
                return MakeResponse(400);
            }
        }
        else if (Request_.Method != EResourceMethod::Options &&
            !_URL.IsResource())
        {
            return MakeResponse(400);
        }

        switch (Request_.Method)
        {
        case EResourceMethod::Head:
            return HeadOrGet(Request_, false);
        case EResourceMethod::Get:
            return HeadOrGet(Request_, true);
        case EResourceMethod::Post:
            return Post(Request_);
        case EResourceMethod::Put:
            return Put(Request_);
        case EResourceMethod::Delete:
            return Delete(Request_);
        case EResourceMethod::Options:
            return Options(Request_);
        default:
        {
            auto _Response = MakeResponse(405);
            SetResourceHeader(_Response.Headers, "Allow", kAllowedMethods);
            return _Response;
        }
        }
    }
    catch (const std::invalid_argument&)
    {
        return MakeResponse(400);
    }
}

iCAX::Resource::CResourceResponse
iCAX::Resource::CResourceAccessService::HeadOrGet(
    IN const CResourceRequest& Request_,
    IN const bool bIncludeBody_)
{
    const auto _Key =
        MakeResourceKeyFromSource(Request_.URL);
    const auto _RequestedVersion =
        ParseOptionalUInt64Header(
            Request_.Headers,
            "ICAX-Resource-Version");
    const auto _Snapshot = _RequestedVersion
        ? m_Library.GetPool().GetSnapshot(
            _Key,
            *_RequestedVersion)
        : m_Library.GetPool().GetSnapshot(_Key);
    if (!_Snapshot)
    {
        return MakeResponse(404);
    }

    const auto _MediaType =
        GetMediaType(_Snapshot->Info, _Snapshot->RuntimeType);
    if (!AcceptsMediaType(
        GetResourceHeader(Request_.Headers, "Accept"),
        _MediaType))
    {
        return MakeResponse(406);
    }

    std::shared_ptr<CFlatBufferResource> _pResource;
    if (_Snapshot->pResource &&
        _Snapshot->RuntimeType &&
        *_Snapshot->RuntimeType == std::type_index(typeid(CFlatBufferResource)))
    {
        _pResource =
            std::static_pointer_cast<CFlatBufferResource>(
                _Snapshot->pResource);
    }

    if (bIncludeBody_ && !_pResource)
    {
        return MakeResponse(406);
    }

    const auto _nContentLength =
        _pResource ? _pResource->Size() : _Snapshot->Info.nSize;
    auto _Response = MakeResponse(200);
    AppendRepresentationHeaders(
        _Response,
        _Snapshot->Info,
        _nContentLength,
        _MediaType);

    const auto _IfNoneMatch =
        GetResourceHeader(Request_.Headers, "If-None-Match");
    if (_IfNoneMatch &&
        ETagListMatches(
            *_IfNoneMatch,
            MakeETag(_Snapshot->Info.nVersion)))
    {
        _Response.nStatus = 304;
        SetResourceHeader(_Response.Headers, "Content-Length", "0");
        return _Response;
    }

    if (bIncludeBody_)
    {
        _Response.Body = *_pResource;
    }
    return _Response;
}

iCAX::Resource::CResourceResponse
iCAX::Resource::CResourceAccessService::Post(
    IN const CResourceRequest& Request_)
{
    if (GetResourceHeader(Request_.Headers, "If-Match") ||
        GetResourceHeader(Request_.Headers, "If-None-Match"))
    {
        return MakeResponse(400);
    }

    const auto _Collection =
        ParseResourceURL(Request_.URL);
    CResourceRequest _PutRequest = Request_;
    _PutRequest.Method = EResourceMethod::Put;
    _PutRequest.URL = iCAX::Resource::MakeResourceURL(
        _Collection.Owner,
        GenerateResourceID());
    SetResourceHeader(
        _PutRequest.Headers,
        "If-None-Match",
        "*");
    return Put(_PutRequest);
}

iCAX::Resource::CResourceResponse
iCAX::Resource::CResourceAccessService::Put(
    IN const CResourceRequest& Request_)
{
    if (GetResourceHeader(
            Request_.Headers,
            "ICAX-Resource-Version"))
    {
        return MakeResponse(400);
    }
    if (Request_.Body.Empty() || !HasPlausibleFlatBufferRoot(Request_.Body))
    {
        return MakeResponse(400);
    }

    const auto _ContentType =
        GetResourceHeader(Request_.Headers, "Content-Type")
        .value_or(kFlatBufferMediaType);
    if (!IsFlatBufferMediaType(_ContentType))
    {
        return MakeResponse(415);
    }

    CResourceInfo _Info;
    _Info.Key = MakeResourceKeyFromSource(Request_.URL);
    _Info.ResourceID =
        ParseResourceURL(Request_.URL).ResourceID;
    _Info.MediaType = _ContentType;
    _Info.ResourceTypeID =
        GetResourceHeader(Request_.Headers, "ICAX-Resource-Type")
        .value_or(CFlatBufferResource::kResourceTypeName);
    _Info.nSchemaVersion =
        ParseOptionalUInt32Header(Request_.Headers, "ICAX-Schema-Version")
        .value_or(0);
    _Info.nMinimumReaderVersion =
        ParseOptionalUInt32Header(Request_.Headers, "ICAX-Min-Reader-Version")
        .value_or(0);
    _Info.FlatBufferIdentifier =
        GetResourceHeader(Request_.Headers, "ICAX-FlatBuffer-Identifier")
        .value_or(std::string());
    _Info.ContentHash =
        GetResourceHeader(Request_.Headers, "ICAX-Content-Hash")
        .value_or(std::string());
    _Info.nSize = Request_.Body.Size();

    if (!_Info.FlatBufferIdentifier.empty())
    {
        if (_Info.FlatBufferIdentifier.size() !=
            flatbuffers::FlatBufferBuilder::kFileIdentifierLength ||
            Request_.Body.Size() <
            sizeof(flatbuffers::uoffset_t) +
            flatbuffers::FlatBufferBuilder::kFileIdentifierLength ||
            !flatbuffers::BufferHasIdentifier(
                Request_.Body.Data(),
                _Info.FlatBufferIdentifier.c_str()))
        {
            return MakeResponse(422);
        }
    }

    const auto _CurrentInfo = m_Library.GetInfo(Request_.URL);
    uint64_t _nExpectedVersion = 0;
    bool _bPreconditionFailed = false;
    const auto _Condition = ResolveWriteCondition(
        Request_,
        _CurrentInfo,
        _nExpectedVersion,
        _bPreconditionFailed);
    if (_bPreconditionFailed)
    {
        return MakeResponse(412);
    }

    auto _pResource =
        std::make_shared<CFlatBufferResource>(Request_.Body);
    CResourceInfo _StoredInfo;
    const auto _Result = m_Library.GetPool().PutUntypedVersioned(
        MakeResourceKeyFromSource(Request_.URL),
        std::static_pointer_cast<void>(_pResource),
        typeid(CFlatBufferResource),
        _Info,
        _Condition,
        _nExpectedVersion,
        &_StoredInfo);
    if (_Result == EResourceMutationResult::PreconditionFailed)
    {
        return MakeResponse(412);
    }

    auto _Response = MakeResponse(
        _Result == EResourceMutationResult::Created ? 201 : 204);
    AppendRepresentationHeaders(
        _Response,
        _StoredInfo,
        Request_.Body.Size(),
        GetMediaType(_StoredInfo, std::type_index(typeid(CFlatBufferResource))));
    SetResourceHeader(_Response.Headers, "Content-Length", "0");
    if (_Result == EResourceMutationResult::Created)
    {
        SetResourceHeader(_Response.Headers, "Location", Request_.URL);
    }
    return _Response;
}

iCAX::Resource::CResourceResponse
iCAX::Resource::CResourceAccessService::Delete(
    IN const CResourceRequest& Request_)
{
    if (GetResourceHeader(
            Request_.Headers,
            "ICAX-Resource-Version"))
    {
        return MakeResponse(400);
    }
    const auto _CurrentInfo = m_Library.GetInfo(Request_.URL);
    if (!_CurrentInfo)
    {
        return MakeResponse(404);
    }

    uint64_t _nExpectedVersion = 0;
    bool _bPreconditionFailed = false;
    const auto _Condition = ResolveWriteCondition(
        Request_,
        _CurrentInfo,
        _nExpectedVersion,
        _bPreconditionFailed);
    if (_bPreconditionFailed)
    {
        return MakeResponse(412);
    }

    const auto _Result = m_Library.GetPool().DeleteCurrentVersioned(
        MakeResourceKeyFromSource(Request_.URL),
        _Condition,
        _nExpectedVersion);
    if (_Result == EResourceMutationResult::NotFound)
    {
        return MakeResponse(404);
    }
    if (_Result == EResourceMutationResult::PreconditionFailed)
    {
        return MakeResponse(412);
    }
    return MakeResponse(204);
}

iCAX::Resource::CResourceResponse
iCAX::Resource::CResourceAccessService::Options(
    IN const CResourceRequest&) const
{
    auto _Response = MakeResponse(204);
    SetResourceHeader(_Response.Headers, "Allow", kAllowedMethods);
    SetResourceHeader(_Response.Headers, "Accept-Put", kFlatBufferMediaType);
    return _Response;
}
