#include "pch.h"
#include "ResourceImportExport.h"

#include <utility>

namespace
{
    std::string _DescribeImportRequest(IN const iCAX::Resource::CResourceImportRequest& Request_)
    {
        return Request_.FormatID.empty() ? Request_.SourcePath : Request_.SourcePath + " (" + Request_.FormatID + ")";
    }

    std::string _DescribeExportRequest(IN const iCAX::Resource::CResourceExportRequest& Request_)
    {
        return Request_.FormatID.empty() ? Request_.ResourceID : Request_.ResourceID + " (" + Request_.FormatID + ")";
    }
}

iCAX::Resource::CResourceImportResult iCAX::Resource::CResourceImportResult::Succeeded(
    IN const std::string& strPrimaryResourceID_,
    IN std::vector<CResourceImportItem> Items_)
{
    CResourceImportResult _Result;
    _Result.Status = EResourceImportExportStatus::Ok;
    _Result.PrimaryResourceID = strPrimaryResourceID_;
    _Result.Items = std::move(Items_);
    return _Result;
}

iCAX::Resource::CResourceImportResult iCAX::Resource::CResourceImportResult::NoHandler(IN const CResourceImportRequest& Request_)
{
    CResourceImportResult _Result;
    _Result.Status = EResourceImportExportStatus::NoHandler;
    _Result.Error = "Resource importer not found: " + _DescribeImportRequest(Request_);
    return _Result;
}

iCAX::Resource::CResourceImportResult iCAX::Resource::CResourceImportResult::Unsupported(
    IN const CResourceImportRequest& Request_,
    IN const std::string& strMessage_)
{
    CResourceImportResult _Result;
    _Result.Status = EResourceImportExportStatus::Unsupported;
    _Result.Error = strMessage_.empty() ? "Resource import source is unsupported: " + _DescribeImportRequest(Request_) : strMessage_;
    return _Result;
}

iCAX::Resource::CResourceImportResult iCAX::Resource::CResourceImportResult::Invalid(
    IN const CResourceImportRequest& Request_,
    IN const std::string& strMessage_)
{
    CResourceImportResult _Result;
    _Result.Status = EResourceImportExportStatus::InvalidRequest;
    _Result.Error = strMessage_.empty() ? "Resource import request is invalid: " + _DescribeImportRequest(Request_) : strMessage_;
    return _Result;
}

iCAX::Resource::CResourceImportResult iCAX::Resource::CResourceImportResult::Failed(
    IN const CResourceImportRequest& Request_,
    IN const std::string& strMessage_)
{
    CResourceImportResult _Result;
    _Result.Status = EResourceImportExportStatus::Failed;
    _Result.Error = strMessage_.empty() ? "Resource import failed: " + _DescribeImportRequest(Request_) : strMessage_;
    return _Result;
}

iCAX::Resource::CResourceExportResult iCAX::Resource::CResourceExportResult::Succeeded(
    IN const std::string& strTargetPath_,
    IN const std::string& strFormatID_,
    IN std::vector<std::string> ResourceIDs_)
{
    CResourceExportResult _Result;
    _Result.Status = EResourceImportExportStatus::Ok;
    _Result.TargetPath = strTargetPath_;
    _Result.FormatID = strFormatID_;
    _Result.ResourceIDs = std::move(ResourceIDs_);
    return _Result;
}

iCAX::Resource::CResourceExportResult iCAX::Resource::CResourceExportResult::NoHandler(IN const CResourceExportRequest& Request_)
{
    CResourceExportResult _Result;
    _Result.Status = EResourceImportExportStatus::NoHandler;
    _Result.Error = "Resource exporter not found: " + _DescribeExportRequest(Request_);
    return _Result;
}

iCAX::Resource::CResourceExportResult iCAX::Resource::CResourceExportResult::Unsupported(
    IN const CResourceExportRequest& Request_,
    IN const std::string& strMessage_)
{
    CResourceExportResult _Result;
    _Result.Status = EResourceImportExportStatus::Unsupported;
    _Result.Error = strMessage_.empty() ? "Resource export target is unsupported: " + _DescribeExportRequest(Request_) : strMessage_;
    return _Result;
}

iCAX::Resource::CResourceExportResult iCAX::Resource::CResourceExportResult::Invalid(
    IN const CResourceExportRequest& Request_,
    IN const std::string& strMessage_)
{
    CResourceExportResult _Result;
    _Result.Status = EResourceImportExportStatus::InvalidRequest;
    _Result.Error = strMessage_.empty() ? "Resource export request is invalid: " + _DescribeExportRequest(Request_) : strMessage_;
    return _Result;
}

iCAX::Resource::CResourceExportResult iCAX::Resource::CResourceExportResult::Failed(
    IN const CResourceExportRequest& Request_,
    IN const std::string& strMessage_)
{
    CResourceExportResult _Result;
    _Result.Status = EResourceImportExportStatus::Failed;
    _Result.Error = strMessage_.empty() ? "Resource export failed: " + _DescribeExportRequest(Request_) : strMessage_;
    return _Result;
}
