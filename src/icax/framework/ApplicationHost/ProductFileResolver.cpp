#include "pch.h"
#include "ProductFileResolver.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace
{
    size_t _GetProbeByteCount(IN const std::vector<iCAX::Product::CProductDefinition>& Products_)
    {
        uint64_t _ProbeByteCount = 0;
        for (const auto& _Product : Products_)
        {
            const auto& _ProjectFile = _Product.ProjectFile;
            if (_ProjectFile.Magic.empty())
            {
                throw std::logic_error("Product project file magic cannot be empty: " + _Product.ProductID);
            }

            const auto _RequiredBytes = _ProjectFile.MagicOffset
                + (std::max<uint64_t>)(
                    _ProjectFile.ProbeBytes,
                    static_cast<uint64_t>(_ProjectFile.Magic.size()));
            _ProbeByteCount = (std::max)(_ProbeByteCount, _RequiredBytes);
        }

        if (_ProbeByteCount == 0)
        {
            return 0;
        }
        return static_cast<size_t>((std::min<uint64_t>)(
            _ProbeByteCount,
            static_cast<uint64_t>(1024 * 1024)));
    }

    std::string _ReadProbeBytes(
        IN const std::string& strProjectPath_,
        IN size_t nProbeBytes_)
    {
        if (nProbeBytes_ == 0)
        {
            return {};
        }

        std::ifstream _Input(strProjectPath_, std::ios::binary);
        if (!_Input)
        {
            throw std::runtime_error("Failed to open project file: " + strProjectPath_);
        }

        std::string _Buffer(nProbeBytes_, '\0');
        _Input.read(_Buffer.data(), static_cast<std::streamsize>(_Buffer.size()));
        _Buffer.resize(static_cast<size_t>(_Input.gcount()));
        return _Buffer;
    }

    bool _HasMagicMatch(
        IN const iCAX::Product::CProductFileDefinition& ProjectFile_,
        IN const std::string& strProbe_)
    {
        if (ProjectFile_.Magic.empty())
        {
            return false;
        }

        const auto _Offset = static_cast<size_t>(ProjectFile_.MagicOffset);
        if (strProbe_.size() < _Offset + ProjectFile_.Magic.size())
        {
            return false;
        }

        return std::equal(
            ProjectFile_.Magic.begin(),
            ProjectFile_.Magic.end(),
            strProbe_.begin() + static_cast<std::ptrdiff_t>(_Offset));
    }

    void _AddCandidate(
        IN OUT std::vector<std::string>& CandidateProductIDs_,
        IN const std::string& strProductID_)
    {
        if (strProductID_.empty())
        {
            return;
        }
        if (std::find(CandidateProductIDs_.begin(), CandidateProductIDs_.end(), strProductID_)
            == CandidateProductIDs_.end())
        {
            CandidateProductIDs_.push_back(strProductID_);
        }
    }
}

iCAX::ApplicationHost::CProductFileResolveResult iCAX::ApplicationHost::CProductFileResolver::Resolve(
    IN const std::string& strProjectPath_,
    IN const std::vector<iCAX::Product::CProductDefinition>& Products_)
{
    if (strProjectPath_.empty())
    {
        throw std::invalid_argument("Project path cannot be empty");
    }
    if (!std::filesystem::exists(strProjectPath_))
    {
        throw std::invalid_argument("Project file does not exist: " + strProjectPath_);
    }
    if (!std::filesystem::is_regular_file(strProjectPath_))
    {
        throw std::invalid_argument("Project path is not a file: " + strProjectPath_);
    }

    CProductFileResolveResult _Result;
    _Result.ProjectPath = strProjectPath_;

    const auto _ProbeBytes = _GetProbeByteCount(Products_);
    const auto _Probe = _ReadProbeBytes(strProjectPath_, _ProbeBytes);

    std::vector<std::string> _MagicMatches;
    for (const auto& _Product : Products_)
    {
        if (_HasMagicMatch(_Product.ProjectFile, _Probe))
        {
            _AddCandidate(_MagicMatches, _Product.ProductID);
        }
    }

    if (_MagicMatches.size() == 1)
    {
        _Result.Status = EProductFileResolveStatus::Resolved;
        _Result.ProductID = _MagicMatches.front();
        _Result.CandidateProductIDs = _MagicMatches;
        _Result.bMatchedByMagic = true;
        return _Result;
    }

    if (_MagicMatches.size() > 1)
    {
        throw std::logic_error("Project file magic matched multiple products: " + strProjectPath_);
    }

    _Result.Status = EProductFileResolveStatus::NotFound;
    return _Result;
}
