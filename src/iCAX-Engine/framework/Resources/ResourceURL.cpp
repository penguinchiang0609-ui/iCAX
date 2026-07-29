#include "pch.h"
#include "ResourceURL.h"

#include <stdexcept>
#include <vector>

namespace
{
    using namespace iCAX::Resource;

    bool IsSafeIDSegment(IN const std::string& strValue_) noexcept
    {
        if (strValue_.empty() ||
            strValue_.find("..") != std::string::npos)
        {
            return false;
        }
        for (const auto _Character : strValue_)
        {
            const bool _bDigit =
                _Character >= '0' && _Character <= '9';
            const bool _bLower =
                _Character >= 'a' && _Character <= 'z';
            const bool _bUpper =
                _Character >= 'A' && _Character <= 'Z';
            const bool _bSymbol =
                _Character == '.' ||
                _Character == '_' ||
                _Character == '-';
            if (!_bDigit && !_bLower && !_bUpper && !_bSymbol)
            {
                return false;
            }
        }
        return true;
    }

    std::vector<std::string> SplitPath(
        IN const std::string& strPath_)
    {
        if (strPath_.empty() || strPath_.ends_with('/'))
        {
            throw std::invalid_argument(
                "Resource URL contains an empty path segment");
        }
        std::vector<std::string> _Segments;
        std::size_t _Begin = 0;
        while (_Begin < strPath_.size())
        {
            const auto _End =
                strPath_.find('/', _Begin);
            auto _Segment = strPath_.substr(
                _Begin,
                _End == std::string::npos
                    ? std::string::npos
                    : _End - _Begin);
            if (_Segment.empty())
            {
                throw std::invalid_argument(
                    "Resource URL contains an empty path segment");
            }
            _Segments.push_back(std::move(_Segment));
            if (_End == std::string::npos)
            {
                break;
            }
            _Begin = _End + 1;
        }
        return _Segments;
    }

    iCAX::Data::uuid ParseCanonicalUUID(
        IN const std::string& strValue_,
        IN const char* pName_)
    {
        const auto _ID =
            iCAX::Data::uuid::from_string(strValue_);
        if (!_ID ||
            _ID->is_nil() ||
            iCAX::Data::to_string(*_ID) != strValue_)
        {
            throw std::invalid_argument(
                std::string(pName_) +
                " must be a canonical non-nil GUID");
        }
        return *_ID;
    }

    void RequireScope(IN const CResourceScope& Scope_)
    {
        if (!Scope_.IsValid())
        {
            throw std::invalid_argument(
                "Resource scope is invalid");
        }
    }
}

bool iCAX::Resource::CResourceScope::IsValid() const noexcept
{
    if (!IsSafeIDSegment(ApplicationID))
    {
        return false;
    }

    switch (Scope)
    {
    case EResourceScope::Application:
        return ProductID.empty() &&
            ProjectID.is_nil() &&
            SceneID.is_nil();
    case EResourceScope::Product:
        return IsSafeIDSegment(ProductID) &&
            ProjectID.is_nil() &&
            SceneID.is_nil();
    case EResourceScope::Project:
        return IsSafeIDSegment(ProductID) &&
            !ProjectID.is_nil() &&
            SceneID.is_nil();
    case EResourceScope::Scene:
        return IsSafeIDSegment(ProductID) &&
            !ProjectID.is_nil() &&
            !SceneID.is_nil();
    default:
        return false;
    }
}

bool iCAX::Resource::CResourceURL::IsCollection() const noexcept
{
    return Owner.IsValid() && ResourceID.is_nil();
}

bool iCAX::Resource::CResourceURL::IsResource() const noexcept
{
    return Owner.IsValid() && !ResourceID.is_nil();
}

iCAX::Resource::CResourceScope
iCAX::Resource::MakeApplicationResourceScope(
    IN const std::string& strApplicationID_)
{
    CResourceScope _Scope;
    _Scope.Scope = EResourceScope::Application;
    _Scope.ApplicationID = strApplicationID_;
    RequireScope(_Scope);
    return _Scope;
}

iCAX::Resource::CResourceScope
iCAX::Resource::MakeProductResourceScope(
    IN const std::string& strApplicationID_,
    IN const std::string& strProductID_)
{
    CResourceScope _Scope;
    _Scope.Scope = EResourceScope::Product;
    _Scope.ApplicationID = strApplicationID_;
    _Scope.ProductID = strProductID_;
    RequireScope(_Scope);
    return _Scope;
}

iCAX::Resource::CResourceScope
iCAX::Resource::MakeProjectResourceScope(
    IN const std::string& strApplicationID_,
    IN const std::string& strProductID_,
    IN const iCAX::Data::uuid& ProjectID_)
{
    CResourceScope _Scope;
    _Scope.Scope = EResourceScope::Project;
    _Scope.ApplicationID = strApplicationID_;
    _Scope.ProductID = strProductID_;
    _Scope.ProjectID = ProjectID_;
    RequireScope(_Scope);
    return _Scope;
}

iCAX::Resource::CResourceScope
iCAX::Resource::MakeSceneResourceScope(
    IN const std::string& strApplicationID_,
    IN const std::string& strProductID_,
    IN const iCAX::Data::uuid& ProjectID_,
    IN const iCAX::Data::uuid& SceneID_)
{
    CResourceScope _Scope;
    _Scope.Scope = EResourceScope::Scene;
    _Scope.ApplicationID = strApplicationID_;
    _Scope.ProductID = strProductID_;
    _Scope.ProjectID = ProjectID_;
    _Scope.SceneID = SceneID_;
    RequireScope(_Scope);
    return _Scope;
}

iCAX::Data::uuid iCAX::Resource::GenerateResourceID()
{
    return iCAX::Data::GenerateNewUUID();
}

std::string iCAX::Resource::MakeResourceCollectionURL(
    IN const CResourceScope& Scope_)
{
    RequireScope(Scope_);

    auto _URL =
        std::string("resource://") +
        Scope_.ApplicationID;
    if (Scope_.Scope >= EResourceScope::Product)
    {
        _URL += "/products/" + Scope_.ProductID;
    }
    if (Scope_.Scope >= EResourceScope::Project)
    {
        _URL +=
            "/projects/" +
            iCAX::Data::to_string(Scope_.ProjectID);
    }
    if (Scope_.Scope >= EResourceScope::Scene)
    {
        _URL +=
            "/scenes/" +
            iCAX::Data::to_string(Scope_.SceneID);
    }
    return _URL + "/resources";
}

std::string iCAX::Resource::MakeResourceURL(
    IN const CResourceScope& Scope_,
    IN const iCAX::Data::uuid& ResourceID_)
{
    if (ResourceID_.is_nil())
    {
        throw std::invalid_argument(
            "ResourceID cannot be nil");
    }
    return MakeResourceCollectionURL(Scope_) +
        "/" +
        iCAX::Data::to_string(ResourceID_);
}

iCAX::Resource::CResourceURL
iCAX::Resource::ParseResourceURL(
    IN const std::string& strURL_)
{
    constexpr const char* kPrefix = "resource://";
    if (!strURL_.starts_with(kPrefix))
    {
        throw std::invalid_argument(
            "Resource URL must use resource://");
    }
    if (strURL_.find_first_of("?#") != std::string::npos)
    {
        throw std::invalid_argument(
            "Resource URL cannot contain query or fragment");
    }

    const auto _Remainder =
        strURL_.substr(std::char_traits<char>::length(kPrefix));
    const auto _Slash = _Remainder.find('/');
    if (_Slash == std::string::npos)
    {
        throw std::invalid_argument(
            "Resource URL has no resources path");
    }

    const auto _ApplicationID =
        _Remainder.substr(0, _Slash);
    if (!IsSafeIDSegment(_ApplicationID))
    {
        throw std::invalid_argument(
            "Resource URL ApplicationID is invalid");
    }
    const auto _Segments =
        SplitPath(_Remainder.substr(_Slash + 1));

    CResourceURL _Result;
    std::size_t _ResourceIndex = 0;
    if (_Segments.size() >= 1 &&
        _Segments[0] == "resources")
    {
        _Result.Owner =
            MakeApplicationResourceScope(_ApplicationID);
        _ResourceIndex = 1;
    }
    else if (_Segments.size() >= 3 &&
        _Segments[0] == "products" &&
        _Segments[2] == "resources")
    {
        _Result.Owner = MakeProductResourceScope(
            _ApplicationID,
            _Segments[1]);
        _ResourceIndex = 3;
    }
    else if (_Segments.size() >= 5 &&
        _Segments[0] == "products" &&
        _Segments[2] == "projects" &&
        _Segments[4] == "resources")
    {
        _Result.Owner = MakeProjectResourceScope(
            _ApplicationID,
            _Segments[1],
            ParseCanonicalUUID(
                _Segments[3],
                "ProjectID"));
        _ResourceIndex = 5;
    }
    else if (_Segments.size() >= 7 &&
        _Segments[0] == "products" &&
        _Segments[2] == "projects" &&
        _Segments[4] == "scenes" &&
        _Segments[6] == "resources")
    {
        _Result.Owner = MakeSceneResourceScope(
            _ApplicationID,
            _Segments[1],
            ParseCanonicalUUID(
                _Segments[3],
                "ProjectID"),
            ParseCanonicalUUID(
                _Segments[5],
                "SceneID"));
        _ResourceIndex = 7;
    }
    else
    {
        throw std::invalid_argument(
            "Resource URL hierarchy is invalid");
    }

    if (_Segments.size() == _ResourceIndex)
    {
        return _Result;
    }
    if (_Segments.size() != _ResourceIndex + 1)
    {
        throw std::invalid_argument(
            "Resource URL has unexpected trailing segments");
    }

    _Result.ResourceID = ParseCanonicalUUID(
        _Segments[_ResourceIndex],
        "ResourceID");
    return _Result;
}

std::optional<iCAX::Resource::CResourceURL>
iCAX::Resource::TryParseResourceURL(
    IN const std::string& strURL_) noexcept
{
    try
    {
        return ParseResourceURL(strURL_);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool iCAX::Resource::ResourceScopeEquals(
    IN const CResourceScope& Left_,
    IN const CResourceScope& Right_) noexcept
{
    return Left_.Scope == Right_.Scope &&
        Left_.ApplicationID == Right_.ApplicationID &&
        Left_.ProductID == Right_.ProductID &&
        Left_.ProjectID == Right_.ProjectID &&
        Left_.SceneID == Right_.SceneID;
}
