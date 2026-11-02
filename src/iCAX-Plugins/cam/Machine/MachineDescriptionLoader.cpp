#include "pch.h"
#include "MachineDescriptionLoader.h"
#include "SDFLoader.h"
#include "URDFLoader.h"
#include <filesystem>
#include <stdexcept>
#include <vector>

bool iCAX::CAM::IsSupportedMachineDescriptionPath(
    IN const std::string& strSourcePath_)
{
    return CanLoadSDFMachineDescription(strSourcePath_)
        || CanLoadURDFMachineDescription(strSourcePath_);
}

bool iCAX::CAM::LoadMachineDescription(
    IN const std::string& strSourcePath_,
    OUT CMachineDescriptionResource& Description_,
    OUT std::string& strError_)
{
    if (CanLoadSDFMachineDescription(strSourcePath_))
    {
        return LoadSDFMachineDescription(strSourcePath_, Description_, strError_);
    }
    if (CanLoadURDFMachineDescription(strSourcePath_))
    {
        return LoadURDFMachineDescription(strSourcePath_, Description_, strError_);
    }

    strError_ = "Unsupported machine description file: " + strSourcePath_;
    return false;
}

std::string iCAX::CAM::ResolveMachineDescriptionResourcePath(
    const CMachineDescriptionResource& description,const std::string& uri)
{
    const auto path=[](const std::string& value){return std::filesystem::path(std::u8string(value.begin(),value.end()));};
    const auto base=description.SourceDirectory.empty()?path(description.SourcePath).parent_path():path(description.SourceDirectory);
    std::vector<std::filesystem::path> candidates;
    if(uri.starts_with("package://")||uri.starts_with("model://")){
        const auto relative=path(uri.substr(uri.find("://")+3));auto first=relative.begin();
        if(first==relative.end())throw std::invalid_argument("Empty machine resource URI");
        const auto package=*first;std::filesystem::path tail;for(++first;first!=relative.end();++first)tail/=*first;
        for(auto directory=base;!directory.empty();){
            candidates.push_back(directory/relative);
            if(directory.filename()==package)candidates.push_back(directory/tail);
            const auto parent=directory.parent_path();if(parent==directory)break;directory=parent;
        }
    }else{
        auto value=uri;if(value.starts_with("file://")){value.erase(0,7);if(value.size()>3&&value[0]=='/'&&value[2]==':')value.erase(0,1);}
        if(value.find("://")!=std::string::npos)throw std::invalid_argument("Machine resource must be a local file: "+uri);
        const auto resource=path(value);candidates.push_back(resource.is_absolute()?resource:base/resource);
    }
    for(const auto& candidate:candidates){std::error_code error;const auto resolved=std::filesystem::canonical(candidate,error);if(!error&&std::filesystem::is_regular_file(resolved)){const auto text=resolved.u8string();return std::string(text.begin(),text.end());}}
    throw std::runtime_error("Machine resource not found: "+uri);
}
