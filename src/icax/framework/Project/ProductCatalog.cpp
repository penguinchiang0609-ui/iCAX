#include "pch.h"
#include "ProductCatalog.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
    struct JsonValue
    {
        enum class Type
        {
            Null,
            String,
            Object,
            Array,
            Literal,
        };

        Type ValueType = Type::Null;
        std::string String;
        std::map<std::string, JsonValue> Object;
        std::vector<JsonValue> Array;
    };

    class JsonParser final
    {
    public:
        explicit JsonParser(IN std::string_view Text_)
            : m_Text(Text_)
        {
        }

        JsonValue Parse()
        {
            auto _Value = ParseValue();
            SkipWhitespace();
            if (m_Position != m_Text.size())
            {
                throw std::runtime_error("Unexpected characters after product manifest");
            }
            return _Value;
        }

    private:
        JsonValue ParseValue()
        {
            SkipWhitespace();
            if (m_Position >= m_Text.size())
            {
                throw std::runtime_error("Unexpected end of product manifest");
            }

            const char _Char = m_Text[m_Position];
            if (_Char == '{')
            {
                return ParseObject();
            }
            if (_Char == '[')
            {
                return ParseArray();
            }
            if (_Char == '"')
            {
                JsonValue _Value;
                _Value.ValueType = JsonValue::Type::String;
                _Value.String = ParseString();
                return _Value;
            }
            return ParseLiteral();
        }

        JsonValue ParseObject()
        {
            Expect('{');

            JsonValue _Value;
            _Value.ValueType = JsonValue::Type::Object;
            SkipWhitespace();
            if (TryConsume('}'))
            {
                return _Value;
            }

            while (true)
            {
                SkipWhitespace();
                const auto _Key = ParseString();
                SkipWhitespace();
                Expect(':');
                _Value.Object.emplace(_Key, ParseValue());
                SkipWhitespace();
                if (TryConsume('}'))
                {
                    break;
                }
                Expect(',');
            }
            return _Value;
        }

        JsonValue ParseArray()
        {
            Expect('[');

            JsonValue _Value;
            _Value.ValueType = JsonValue::Type::Array;
            SkipWhitespace();
            if (TryConsume(']'))
            {
                return _Value;
            }

            while (true)
            {
                _Value.Array.push_back(ParseValue());
                SkipWhitespace();
                if (TryConsume(']'))
                {
                    break;
                }
                Expect(',');
            }
            return _Value;
        }

        JsonValue ParseLiteral()
        {
            const auto _Start = m_Position;
            while (m_Position < m_Text.size()
                && m_Text[m_Position] != ','
                && m_Text[m_Position] != '}'
                && m_Text[m_Position] != ']'
                && !std::isspace(static_cast<unsigned char>(m_Text[m_Position])))
            {
                ++m_Position;
            }
            if (_Start == m_Position)
            {
                throw std::runtime_error("Expected JSON value");
            }

            JsonValue _Value;
            _Value.ValueType = JsonValue::Type::Literal;
            _Value.String = std::string(m_Text.substr(_Start, m_Position - _Start));
            return _Value;
        }

        std::string ParseString()
        {
            Expect('"');
            std::string _Result;
            while (m_Position < m_Text.size())
            {
                const char _Char = m_Text[m_Position++];
                if (_Char == '"')
                {
                    return _Result;
                }
                if (_Char == '\\')
                {
                    if (m_Position >= m_Text.size())
                    {
                        throw std::runtime_error("Invalid JSON escape");
                    }
                    const char _Escaped = m_Text[m_Position++];
                    switch (_Escaped)
                    {
                    case '"': _Result.push_back('"'); break;
                    case '\\': _Result.push_back('\\'); break;
                    case '/': _Result.push_back('/'); break;
                    case 'b': _Result.push_back('\b'); break;
                    case 'f': _Result.push_back('\f'); break;
                    case 'n': _Result.push_back('\n'); break;
                    case 'r': _Result.push_back('\r'); break;
                    case 't': _Result.push_back('\t'); break;
                    case 'u':
                        _Result.append("\\u");
                        for (int _Index = 0; _Index < 4 && m_Position < m_Text.size(); ++_Index)
                        {
                            _Result.push_back(m_Text[m_Position++]);
                        }
                        break;
                    default:
                        _Result.push_back(_Escaped);
                        break;
                    }
                }
                else
                {
                    _Result.push_back(_Char);
                }
            }
            throw std::runtime_error("Unterminated JSON string");
        }

        void SkipWhitespace()
        {
            while (m_Position < m_Text.size()
                && std::isspace(static_cast<unsigned char>(m_Text[m_Position])))
            {
                ++m_Position;
            }
        }

        bool TryConsume(IN char Char_)
        {
            SkipWhitespace();
            if (m_Position < m_Text.size() && m_Text[m_Position] == Char_)
            {
                ++m_Position;
                return true;
            }
            return false;
        }

        void Expect(IN char Char_)
        {
            SkipWhitespace();
            if (m_Position >= m_Text.size() || m_Text[m_Position] != Char_)
            {
                throw std::runtime_error(std::string("Expected '") + Char_ + "'");
            }
            ++m_Position;
        }

    private:
        std::string_view m_Text;
        size_t m_Position = 0;
    };

    using JsonObject = std::map<std::string, JsonValue>;

    JsonObject RequireObject(IN const JsonValue& Value_, IN const std::string& strLabel_)
    {
        if (Value_.ValueType != JsonValue::Type::Object)
        {
            throw std::runtime_error(strLabel_ + " must be an object");
        }
        return Value_.Object;
    }

    std::string OptionalString(IN const JsonObject& Object_, IN const std::string& strKey_)
    {
        auto _Iter = Object_.find(strKey_);
        if (_Iter == Object_.end())
        {
            return {};
        }
        if (_Iter->second.ValueType != JsonValue::Type::String)
        {
            throw std::runtime_error(strKey_ + " must be a string");
        }
        return _Iter->second.String;
    }

    JsonObject OptionalObject(IN const JsonObject& Object_, IN const std::string& strKey_)
    {
        auto _Iter = Object_.find(strKey_);
        if (_Iter == Object_.end())
        {
            return {};
        }
        return RequireObject(_Iter->second, strKey_);
    }

    std::vector<std::string> OptionalStringArray(IN const JsonObject& Object_, IN const std::string& strKey_)
    {
        auto _Iter = Object_.find(strKey_);
        if (_Iter == Object_.end())
        {
            return {};
        }
        if (_Iter->second.ValueType != JsonValue::Type::Array)
        {
            throw std::runtime_error(strKey_ + " must be an array");
        }

        std::vector<std::string> _Result;
        for (const auto& _Item : _Iter->second.Array)
        {
            if (_Item.ValueType != JsonValue::Type::String)
            {
                throw std::runtime_error(strKey_ + " must contain strings");
            }
            _Result.push_back(_Item.String);
        }
        return _Result;
    }

    std::vector<iCAX::Project::CProductModule> OptionalPluginModules(IN const JsonObject& Object_, IN const std::string& strKey_)
    {
        auto _Iter = Object_.find(strKey_);
        if (_Iter == Object_.end())
        {
            return {};
        }
        if (_Iter->second.ValueType != JsonValue::Type::Array)
        {
            throw std::runtime_error(strKey_ + " must be an array");
        }

        std::vector<iCAX::Project::CProductModule> _Modules;
        for (const auto& _Item : _Iter->second.Array)
        {
            auto _Object = RequireObject(_Item, strKey_ + "[]");
            iCAX::Project::CProductModule _Module;
            _Module.Name = OptionalString(_Object, "name");
            _Module.ComponentPath = OptionalString(_Object, "componentPath");
            _Module.BehaviourPath = OptionalString(_Object, "behaviourPath");
            _Module.ServicePath = OptionalString(_Object, "servicePath");
            _Module.CommandPath = OptionalString(_Object, "commandPath");
            _Modules.push_back(_Module);
        }
        return _Modules;
    }

    std::string ResolveManifestPath(IN const std::filesystem::path& Base_, IN const std::string& strPath_)
    {
        if (strPath_.empty())
        {
            return {};
        }

        std::filesystem::path _Path(strPath_);
        if (!_Path.is_absolute())
        {
            _Path = Base_ / _Path;
        }
        return _Path.lexically_normal().string();
    }

    std::vector<std::string> ResolveManifestPaths(IN const std::filesystem::path& Base_, IN const std::vector<std::string>& Paths_)
    {
        std::vector<std::string> _Resolved;
        _Resolved.reserve(Paths_.size());
        for (const auto& _Path : Paths_)
        {
            _Resolved.push_back(ResolveManifestPath(Base_, _Path));
        }
        return _Resolved;
    }

    std::vector<iCAX::Project::CProductModule> ResolvePluginModules(
        IN const std::filesystem::path& Base_,
        IN const std::vector<iCAX::Project::CProductModule>& Modules_)
    {
        std::vector<iCAX::Project::CProductModule> _Resolved;
        _Resolved.reserve(Modules_.size());
        for (auto _Module : Modules_)
        {
            _Module.ComponentPath = ResolveManifestPath(Base_, _Module.ComponentPath);
            _Module.BehaviourPath = ResolveManifestPath(Base_, _Module.BehaviourPath);
            _Module.ServicePath = ResolveManifestPath(Base_, _Module.ServicePath);
            _Module.CommandPath = ResolveManifestPath(Base_, _Module.CommandPath);
            _Resolved.push_back(_Module);
        }
        return _Resolved;
    }

    JsonValue LoadJsonFile(IN const std::string& strPath_)
    {
        std::ifstream _Input(strPath_, std::ios::binary);
        if (!_Input)
        {
            throw std::runtime_error("Failed to open product manifest: " + strPath_);
        }

        std::ostringstream _Buffer;
        _Buffer << _Input.rdbuf();
        auto _Text = _Buffer.str();
        if (_Text.size() >= 3
            && static_cast<unsigned char>(_Text[0]) == 0xEF
            && static_cast<unsigned char>(_Text[1]) == 0xBB
            && static_cast<unsigned char>(_Text[2]) == 0xBF)
        {
            _Text.erase(0, 3);
        }
        return JsonParser(_Text).Parse();
    }
}

bool iCAX::Project::CProductCatalog::Register(IN const CProductDefinition& Definition_)
{
    if (!Definition_.IsValid())
    {
        throw std::invalid_argument("ProductID cannot be empty");
    }
    return m_Products.emplace(Definition_.ProductID, Definition_).second;
}

void iCAX::Project::CProductCatalog::Set(IN const CProductDefinition& Definition_)
{
    if (!Definition_.IsValid())
    {
        throw std::invalid_argument("ProductID cannot be empty");
    }
    m_Products[Definition_.ProductID] = Definition_;
}

bool iCAX::Project::CProductCatalog::Has(IN const std::string& strProductID_) const
{
    return m_Products.find(strProductID_) != m_Products.end();
}

std::shared_ptr<const iCAX::Project::CProductDefinition> iCAX::Project::CProductCatalog::Find(IN const std::string& strProductID_) const
{
    auto _Iter = m_Products.find(strProductID_);
    if (_Iter == m_Products.end())
    {
        return nullptr;
    }
    return std::make_shared<CProductDefinition>(_Iter->second);
}

iCAX::Project::CProductDefinition iCAX::Project::CProductCatalog::Require(IN const std::string& strProductID_) const
{
    auto _Iter = m_Products.find(strProductID_);
    if (_Iter == m_Products.end())
    {
        throw std::runtime_error("Product not found: " + strProductID_);
    }
    return _Iter->second;
}

std::vector<iCAX::Project::CProductDefinition> iCAX::Project::CProductCatalog::GetDefinitions() const
{
    std::vector<CProductDefinition> _Definitions;
    _Definitions.reserve(m_Products.size());
    for (const auto& [_ID, _Definition] : m_Products)
    {
        _Definitions.push_back(_Definition);
    }
    return _Definitions;
}

std::vector<std::string> iCAX::Project::CProductCatalog::GetProductIDs() const
{
    std::vector<std::string> _IDs;
    _IDs.reserve(m_Products.size());
    for (const auto& [_ID, _Definition] : m_Products)
    {
        _IDs.push_back(_ID);
    }
    return _IDs;
}

size_t iCAX::Project::CProductCatalog::Count() const
{
    return m_Products.size();
}

void iCAX::Project::CProductCatalog::Clear()
{
    m_Products.clear();
}

iCAX::Project::CProductDefinition iCAX::Project::CProductCatalog::LoadManifestFile(IN const std::string& strPath_) const
{
    auto _Root = RequireObject(LoadJsonFile(strPath_), "product manifest");
    const std::filesystem::path _ManifestPath(strPath_);
    const auto _ProductRoot = _ManifestPath.parent_path();

    CProductDefinition _Definition;
    _Definition.ProductID = OptionalString(_Root, "productId");
    if (_Definition.ProductID.empty())
    {
        throw std::runtime_error("product manifest requires productId: " + strPath_);
    }

    _Definition.DisplayName = OptionalString(_Root, "displayName");
    _Definition.ManifestPath = _ManifestPath.lexically_normal().string();
    _Definition.ProductRootPath = _ProductRoot.lexically_normal().string();

    auto _Backend = OptionalObject(_Root, "backend");
    _Definition.ComponentModules = ResolveManifestPaths(_ProductRoot, OptionalStringArray(_Backend, "components"));
    _Definition.BehaviourModules = ResolveManifestPaths(_ProductRoot, OptionalStringArray(_Backend, "behaviours"));
    _Definition.ServiceModules = ResolveManifestPaths(_ProductRoot, OptionalStringArray(_Backend, "services"));
    _Definition.CommandModules = ResolveManifestPaths(_ProductRoot, OptionalStringArray(_Backend, "commands"));
    _Definition.PluginModules = ResolvePluginModules(_ProductRoot, OptionalPluginModules(_Backend, "plugins"));

    auto _Frontend = OptionalObject(_Root, "frontend");
    _Definition.FrontendEntry = ResolveManifestPath(_ProductRoot, OptionalString(_Frontend, "entry"));

    auto _Protocol = OptionalObject(_Root, "protocol");
    _Definition.ProtocolPath = ResolveManifestPath(_ProductRoot, OptionalString(_Protocol, "path"));

    auto _Startup = OptionalObject(_Root, "startup");
    _Definition.StartupComponent = OptionalString(_Startup, "firstComponent");

    auto _File = OptionalObject(_Root, "file");
    _Definition.FileExtensions = OptionalStringArray(_File, "extensions");

    return _Definition;
}

std::vector<iCAX::Project::CProductDefinition> iCAX::Project::CProductCatalog::LoadManifestDirectory(IN const std::string& strDirectory_) const
{
    std::vector<CProductDefinition> _Definitions;
    if (strDirectory_.empty())
    {
        return _Definitions;
    }

    const std::filesystem::path _Root(strDirectory_);
    if (!std::filesystem::exists(_Root))
    {
        return _Definitions;
    }

    if (std::filesystem::is_regular_file(_Root))
    {
        _Definitions.push_back(LoadManifestFile(_Root.string()));
        return _Definitions;
    }

    for (const auto& _Entry : std::filesystem::directory_iterator(_Root))
    {
        if (!_Entry.is_directory())
        {
            continue;
        }
        const auto _ManifestPath = _Entry.path() / "manifest.json";
        if (std::filesystem::exists(_ManifestPath))
        {
            _Definitions.push_back(LoadManifestFile(_ManifestPath.string()));
        }
    }
    return _Definitions;
}
