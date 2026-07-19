#include "pch.h"
#include "ProductData.h"

#include "Data/VariantSerializer.h"
#include "ProductDefinition.h"


namespace
{
    std::filesystem::path _PathFromUTF8(IN const std::string& strPath_)
    {
        std::u8string _Text(strPath_.begin(), strPath_.end());
        return std::filesystem::path(_Text);
    }

    std::string _PathToUTF8(IN const std::filesystem::path& Path_)
    {
        const auto _Text = Path_.u8string();
        return std::string(_Text.begin(), _Text.end());
    }

    std::string _GetString(
        IN const iCAX::Data::ObjectMap& Object_,
        IN const std::string& strName_,
        IN const std::string& strDefault_ = std::string())
    {
        auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return strDefault_;
        }
        if (!_Iter->second.Is<std::string>())
        {
            throw std::runtime_error("Product data field must be a string: " + strName_);
        }
        return _Iter->second.To<std::string>();
    }

    iCAX::Data::Variant _RecentProjectToVariant(IN const iCAX::Product::CRecentProjectItem& Item_)
    {
        iCAX::Data::ObjectMap _Object;
        _Object["path"] = Item_.ProjectPath;
        _Object["displayName"] = Item_.DisplayName;
        _Object["lastOpenedTime"] = Item_.LastOpenedTime;
        return iCAX::Data::Variant(_Object);
    }

    iCAX::Product::CRecentProjectItem _RecentProjectFromVariant(IN const iCAX::Data::Variant& Variant_)
    {
        if (!Variant_.Is<iCAX::Data::ObjectMap>())
        {
            throw std::runtime_error("Product recent project item must be an object");
        }

        const auto _Object = Variant_.To<iCAX::Data::ObjectMap>();
        iCAX::Product::CRecentProjectItem _Item;
        _Item.ProjectPath = _GetString(_Object, "path");
        _Item.DisplayName = _GetString(_Object, "displayName");
        _Item.LastOpenedTime = _GetString(_Object, "lastOpenedTime");
        return _Item;
    }

    iCAX::Data::Variant _ProductDataToVariant(IN const iCAX::Product::CProductData& Data_)
    {
        iCAX::Data::ObjectMap _Root;

        iCAX::Data::VariantArray _RecentProjects;
        _RecentProjects.reserve(Data_.RecentProjects.size());
        for (const auto& _Item : Data_.RecentProjects)
        {
            _RecentProjects.emplace_back(_RecentProjectToVariant(_Item));
        }
        _Root["recentProjects"] = _RecentProjects;
        _Root["settings"] = Data_.Settings.GetProperties();
        return iCAX::Data::Variant(_Root);
    }

    iCAX::Product::CProductData _ProductDataFromVariant(IN const iCAX::Data::Variant& Variant_)
    {
        if (!Variant_.Is<iCAX::Data::ObjectMap>())
        {
            throw std::runtime_error("Product data root must be an object");
        }

        const auto _Root = Variant_.To<iCAX::Data::ObjectMap>();
        iCAX::Product::CProductData _Data;

        auto _RecentIter = _Root.find("recentProjects");
        if (_RecentIter != _Root.end() && !_RecentIter->second.Is<std::monostate>())
        {
            if (!_RecentIter->second.Is<iCAX::Data::VariantArray>())
            {
                throw std::runtime_error("Product recentProjects must be an array");
            }

            for (const auto& _ItemVariant : _RecentIter->second.To<iCAX::Data::VariantArray>())
            {
                auto _Item = _RecentProjectFromVariant(_ItemVariant);
                if (!_Item.ProjectPath.empty())
                {
                    _Data.RecentProjects.emplace_back(std::move(_Item));
                }
            }
        }

        auto _SettingsIter = _Root.find("settings");
        if (_SettingsIter != _Root.end() && !_SettingsIter->second.Is<std::monostate>())
        {
            if (!_SettingsIter->second.Is<iCAX::Data::ObjectMap>())
            {
                throw std::runtime_error("Product settings must be an object");
            }
            _Data.Settings = iCAX::Data::PropertyBag(_SettingsIter->second.To<iCAX::Data::ObjectMap>());
        }

        return _Data;
    }
}

iCAX::Product::CFileProductDataStore::CFileProductDataStore(IN std::string strProductDataRoot_)
    : m_strProductDataRoot(std::move(strProductDataRoot_))
{
    if (m_strProductDataRoot.empty())
    {
        m_strProductDataRoot = "Setting/Products";
    }
}

iCAX::Product::CProductData iCAX::Product::CFileProductDataStore::Load(IN const std::string& strProductID_) const
{
    const auto _Path = GetProductDataPath(strProductID_);
    const auto _FilePath = _PathFromUTF8(_Path);
    if (!std::filesystem::exists(_FilePath))
    {
        return CProductData();
    }

    std::ifstream _Input(_FilePath, std::ios::binary);
    if (!_Input)
    {
        throw std::runtime_error("Failed to open product data file: " + _Path);
    }

    std::string _Text{
        std::istreambuf_iterator<char>(_Input),
        std::istreambuf_iterator<char>()};
    return _ProductDataFromVariant(iCAX::Data::VariantSerializer::Deserialize(_Text));
}

void iCAX::Product::CFileProductDataStore::Save(
    IN const std::string& strProductID_,
    IN const CProductData& Data_) const
{
    const auto _Path = GetProductDataPath(strProductID_);
    const auto _FilePath = _PathFromUTF8(_Path);
    auto _ParentPath = _FilePath.parent_path();
    if (!_ParentPath.empty())
    {
        std::filesystem::create_directories(_ParentPath);
    }

    std::ofstream _Output(_FilePath, std::ios::binary | std::ios::trunc);
    if (!_Output)
    {
        throw std::runtime_error("Failed to write product data file: " + _Path);
    }

    _Output << iCAX::Data::VariantSerializer::Serialize(_ProductDataToVariant(Data_));
}

std::string iCAX::Product::CFileProductDataStore::GetProductDataPath(IN const std::string& strProductID_) const
{
    if (!IsValidProductID(strProductID_))
    {
        throw std::invalid_argument("ProductID contains unsafe characters: " + strProductID_);
    }

    return _PathToUTF8(_PathFromUTF8(m_strProductDataRoot) / strProductID_ / "Product.Data");
}
