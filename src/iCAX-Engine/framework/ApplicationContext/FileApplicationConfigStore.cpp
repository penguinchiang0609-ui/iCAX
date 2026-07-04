#include "pch.h"
#include "FileApplicationConfigStore.h"

#include <Data/VariantSerializer.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

iCAX::Data::PropertyBag iCAX::Application::CFileApplicationConfigStore::Load(IN const std::string& strPath_) const
{
    if (strPath_.empty())
    {
        throw std::invalid_argument("Application config path cannot be empty");
    }

    if (!std::filesystem::exists(strPath_))
    {
        return iCAX::Data::PropertyBag();
    }

    std::ifstream _Input(strPath_, std::ios::binary);
    if (!_Input)
    {
        throw std::runtime_error("Failed to open application config file: " + strPath_);
    }

    std::ostringstream _Buffer;
    _Buffer << _Input.rdbuf();
    auto _Variant = iCAX::Data::VariantSerializer::Deserialize(_Buffer.str());
    if (!_Variant.Is<iCAX::Data::ObjectMap>())
    {
        throw std::runtime_error("Application config root must be an object: " + strPath_);
    }

    return iCAX::Data::PropertyBag(_Variant.To<iCAX::Data::ObjectMap>());
}

void iCAX::Application::CFileApplicationConfigStore::Save(IN const std::string& strPath_, IN const iCAX::Data::PropertyBag& Settings_) const
{
    if (strPath_.empty())
    {
        throw std::invalid_argument("Application config path cannot be empty");
    }

    auto _ParentPath = std::filesystem::path(strPath_).parent_path();
    if (!_ParentPath.empty())
    {
        std::filesystem::create_directories(_ParentPath);
    }

    std::ofstream _Output(strPath_, std::ios::binary | std::ios::trunc);
    if (!_Output)
    {
        throw std::runtime_error("Failed to write application config file: " + strPath_);
    }

    _Output << iCAX::Data::VariantSerializer::Serialize(iCAX::Data::Variant(Settings_.GetProperties()));
}
