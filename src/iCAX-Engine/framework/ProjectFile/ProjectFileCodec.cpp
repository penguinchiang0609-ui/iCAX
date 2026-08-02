#include "pch.h"

#include "ProjectFileCodec.h"

#include "Data/VariantSerializer.h"

namespace
{
    using namespace iCAX::ProjectFile;

    constexpr std::string_view kASCIIIdentifier =
        "ICAX-PROJECT-ASCII;";
    constexpr std::array<uint8_t, 8> kBinaryIdentifier =
        {'I', 'C', 'A', 'X', 'P', 'B', 'I', 'N'};
    constexpr uint64_t kMaximumStringSize = 64ull * 1024ull * 1024ull;
    constexpr uint64_t kMaximumRecordCount = 10'000'000ull;

    enum class EBinaryRecord : uint8_t
    {
        Entity = 1,
        Component = 2,
        Resource = 3,
        ResourceDependency = 4,
        ResourceBody = 5,
    };

    std::string_view Trim(IN std::string_view Value_)
    {
        while (!Value_.empty() &&
            std::isspace(
                static_cast<unsigned char>(Value_.front())))
        {
            Value_.remove_prefix(1);
        }
        while (!Value_.empty() &&
            std::isspace(
                static_cast<unsigned char>(Value_.back())))
        {
            Value_.remove_suffix(1);
        }
        return Value_;
    }

    std::string Quote(IN std::string_view Value_)
    {
        std::string _Result;
        _Result.reserve(Value_.size() + 2);
        _Result.push_back('"');
        for (const auto _Character : Value_)
        {
            switch (_Character)
            {
            case '\\': _Result += "\\\\"; break;
            case '"': _Result += "\\\""; break;
            case '\n': _Result += "\\n"; break;
            case '\r': _Result += "\\r"; break;
            case '\t': _Result += "\\t"; break;
            case '\b': _Result += "\\b"; break;
            case '\f': _Result += "\\f"; break;
            default:
                if (static_cast<unsigned char>(_Character) < 0x20)
                {
                    throw std::invalid_argument(
                        "ASCII project value contains an unsupported control character");
                }
                _Result.push_back(_Character);
                break;
            }
        }
        _Result.push_back('"');
        return _Result;
    }

    struct CASCIIRecord final
    {
        std::string Name;
        std::vector<std::string> Arguments;
    };

    CASCIIRecord ParseASCIIRecord(
        IN std::string_view Line_,
        IN const size_t nLine_)
    {
        Line_ = Trim(Line_);
        if (Line_.empty() || Line_.back() != ';')
        {
            throw std::invalid_argument(
                "Malformed ASCII project record at line " +
                std::to_string(nLine_));
        }
        Line_.remove_suffix(1);
        const auto _Open = Line_.find('(');
        const auto _Close = Line_.rfind(')');
        if (_Open == std::string_view::npos ||
            _Close == std::string_view::npos ||
            _Close < _Open ||
            !Trim(Line_.substr(_Close + 1)).empty())
        {
            throw std::invalid_argument(
                "Malformed ASCII project record at line " +
                std::to_string(nLine_));
        }

        CASCIIRecord _Record;
        _Record.Name = std::string(Trim(Line_.substr(0, _Open)));
        if (_Record.Name.empty())
        {
            throw std::invalid_argument(
                "ASCII project record name is empty at line " +
                std::to_string(nLine_));
        }

        auto _Arguments = Line_.substr(
            _Open + 1,
            _Close - _Open - 1);
        size_t _Position = 0;
        while (_Position < _Arguments.size())
        {
            while (_Position < _Arguments.size() &&
                std::isspace(static_cast<unsigned char>(
                    _Arguments[_Position])))
            {
                ++_Position;
            }
            if (_Position == _Arguments.size())
            {
                break;
            }

            std::string _Value;
            if (_Arguments[_Position] == '"')
            {
                ++_Position;
                bool _Closed = false;
                while (_Position < _Arguments.size())
                {
                    const auto _Character =
                        _Arguments[_Position++];
                    if (_Character == '"')
                    {
                        _Closed = true;
                        break;
                    }
                    if (_Character != '\\')
                    {
                        _Value.push_back(_Character);
                        continue;
                    }
                    if (_Position == _Arguments.size())
                    {
                        throw std::invalid_argument(
                            "Truncated ASCII project escape at line " +
                            std::to_string(nLine_));
                    }
                    switch (_Arguments[_Position++])
                    {
                    case '\\': _Value.push_back('\\'); break;
                    case '"': _Value.push_back('"'); break;
                    case 'n': _Value.push_back('\n'); break;
                    case 'r': _Value.push_back('\r'); break;
                    case 't': _Value.push_back('\t'); break;
                    case 'b': _Value.push_back('\b'); break;
                    case 'f': _Value.push_back('\f'); break;
                    default:
                        throw std::invalid_argument(
                            "Unsupported ASCII project escape at line " +
                            std::to_string(nLine_));
                    }
                }
                if (!_Closed)
                {
                    throw std::invalid_argument(
                        "Unterminated ASCII project string at line " +
                        std::to_string(nLine_));
                }
            }
            else
            {
                const auto _Comma =
                    _Arguments.find(',', _Position);
                const auto _End = _Comma ==
                    std::string_view::npos
                    ? _Arguments.size()
                    : _Comma;
                _Value = std::string(Trim(
                    _Arguments.substr(
                        _Position,
                        _End - _Position)));
                _Position = _End;
                if (_Value.empty())
                {
                    throw std::invalid_argument(
                        "Empty ASCII project argument at line " +
                        std::to_string(nLine_));
                }
            }

            while (_Position < _Arguments.size() &&
                std::isspace(static_cast<unsigned char>(
                    _Arguments[_Position])))
            {
                ++_Position;
            }
            _Record.Arguments.push_back(std::move(_Value));
            if (_Position == _Arguments.size())
            {
                break;
            }
            if (_Arguments[_Position] != ',')
            {
                throw std::invalid_argument(
                    "Expected ASCII project argument separator at line " +
                    std::to_string(nLine_));
            }
            ++_Position;
            if (_Position == _Arguments.size())
            {
                throw std::invalid_argument(
                    "Trailing ASCII project argument separator at line " +
                    std::to_string(nLine_));
            }
        }
        return _Record;
    }

    void RequireArgumentCount(
        IN const CASCIIRecord& Record_,
        IN const size_t nExpected_,
        IN const size_t nLine_)
    {
        if (Record_.Arguments.size() != nExpected_)
        {
            throw std::invalid_argument(
                "Unexpected argument count for " +
                Record_.Name + " at line " +
                std::to_string(nLine_));
        }
    }

    uint64_t ParseUInt64(
        IN const std::string& Value_,
        IN std::string_view Description_)
    {
        size_t _Consumed = 0;
        uint64_t _Result = 0;
        try
        {
            _Result = std::stoull(Value_, &_Consumed, 10);
        }
        catch (const std::exception&)
        {
            throw std::invalid_argument(
                std::string(Description_) + " is not an unsigned integer");
        }
        if (_Consumed != Value_.size())
        {
            throw std::invalid_argument(
                std::string(Description_) + " is not an unsigned integer");
        }
        return _Result;
    }

    uint32_t ParseUInt32(
        IN const std::string& Value_,
        IN std::string_view Description_)
    {
        const auto _Value = ParseUInt64(Value_, Description_);
        if (_Value > std::numeric_limits<uint32_t>::max())
        {
            throw std::invalid_argument(
                std::string(Description_) + " exceeds uint32");
        }
        return static_cast<uint32_t>(_Value);
    }

    iCAX::Data::uuid ParseUUID(
        IN const std::string& Value_,
        IN std::string_view Description_)
    {
        const auto _Result =
            iCAX::Data::uuid::from_string(Value_);
        if (!_Result)
        {
            throw std::invalid_argument(
                std::string(Description_) + " is not a UUID");
        }
        return *_Result;
    }

    iCAX::Data::ObjectMap ParseObjectMap(
        IN const std::string& Value_,
        IN std::string_view Description_)
    {
        auto _Value =
            iCAX::Data::VariantSerializer::Deserialize(Value_);
        if (!_Value.Is<iCAX::Data::ObjectMap>())
        {
            throw std::invalid_argument(
                std::string(Description_) + " is not an object map");
        }
        return _Value.To<iCAX::Data::ObjectMap>();
    }

    std::string SerializeObjectMap(
        IN const iCAX::Data::ObjectMap& Value_)
    {
        return iCAX::Data::VariantSerializer::Serialize(
            iCAX::Data::Variant(Value_));
    }

    iCAX::Data::ObjectMap MetadataToObject(
        IN const std::map<std::string, std::string>& Metadata_)
    {
        iCAX::Data::ObjectMap _Result;
        for (const auto& [_Key, _Value] : Metadata_)
        {
            _Result[_Key] = _Value;
        }
        return _Result;
    }

    std::map<std::string, std::string> ObjectToMetadata(
        IN const iCAX::Data::ObjectMap& Object_)
    {
        std::map<std::string, std::string> _Result;
        for (const auto& [_Key, _Value] : Object_)
        {
            if (!_Value.Is<std::string>())
            {
                throw std::invalid_argument(
                    "Resource metadata values must be strings");
            }
            _Result.emplace(_Key, _Value.To<std::string>());
        }
        return _Result;
    }

    std::string Base64Encode(IN std::span<const uint8_t> Bytes_)
    {
        static constexpr char _Alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string _Result;
        _Result.reserve(((Bytes_.size() + 2) / 3) * 4);
        for (size_t i = 0; i < Bytes_.size(); i += 3)
        {
            const uint32_t _A = Bytes_[i];
            const uint32_t _B = i + 1 < Bytes_.size()
                ? Bytes_[i + 1] : 0;
            const uint32_t _C = i + 2 < Bytes_.size()
                ? Bytes_[i + 2] : 0;
            const uint32_t _Value =
                (_A << 16) | (_B << 8) | _C;
            _Result.push_back(_Alphabet[(_Value >> 18) & 0x3f]);
            _Result.push_back(_Alphabet[(_Value >> 12) & 0x3f]);
            _Result.push_back(i + 1 < Bytes_.size()
                ? _Alphabet[(_Value >> 6) & 0x3f] : '=');
            _Result.push_back(i + 2 < Bytes_.size()
                ? _Alphabet[_Value & 0x3f] : '=');
        }
        return _Result;
    }

    std::vector<uint8_t> Base64Decode(IN std::string_view Text_)
    {
        if (Text_.size() % 4 != 0)
        {
            throw std::invalid_argument(
                "Resource body is not valid base64");
        }
        auto _Decode = [](const char Character_) -> int
        {
            if (Character_ >= 'A' && Character_ <= 'Z')
                return Character_ - 'A';
            if (Character_ >= 'a' && Character_ <= 'z')
                return Character_ - 'a' + 26;
            if (Character_ >= '0' && Character_ <= '9')
                return Character_ - '0' + 52;
            if (Character_ == '+') return 62;
            if (Character_ == '/') return 63;
            return -1;
        };

        std::vector<uint8_t> _Result;
        _Result.reserve((Text_.size() / 4) * 3);
        for (size_t i = 0; i < Text_.size(); i += 4)
        {
            const bool _Last = i + 4 == Text_.size();
            const bool _Pad2 = Text_[i + 2] == '=';
            const bool _Pad3 = Text_[i + 3] == '=';
            if ((!_Last && (_Pad2 || _Pad3)) ||
                (_Pad2 && !_Pad3))
            {
                throw std::invalid_argument(
                    "Resource body has invalid base64 padding");
            }
            const int _A = _Decode(Text_[i]);
            const int _B = _Decode(Text_[i + 1]);
            const int _C = _Pad2 ? 0 : _Decode(Text_[i + 2]);
            const int _D = _Pad3 ? 0 : _Decode(Text_[i + 3]);
            if (_A < 0 || _B < 0 || _C < 0 || _D < 0)
            {
                throw std::invalid_argument(
                    "Resource body is not valid base64");
            }
            const uint32_t _Value =
                (static_cast<uint32_t>(_A) << 18) |
                (static_cast<uint32_t>(_B) << 12) |
                (static_cast<uint32_t>(_C) << 6) |
                static_cast<uint32_t>(_D);
            _Result.push_back(
                static_cast<uint8_t>(_Value >> 16));
            if (!_Pad2)
            {
                _Result.push_back(
                    static_cast<uint8_t>(_Value >> 8));
            }
            if (!_Pad3)
            {
                _Result.push_back(
                    static_cast<uint8_t>(_Value));
            }
        }
        return _Result;
    }

    CProjectResourceReference ParseReference(
        IN const std::string& URL_,
        IN const std::string& Version_)
    {
        return {
            URL_,
            ParseUInt64(Version_, "Resource version")};
    }

    std::string EncodeASCII(IN const CProjectDocument& Document_)
    {
        std::ostringstream _Output;
        _Output << Document_.Info.Magic << '\n';
        _Output << kASCIIIdentifier << '\n';
        _Output << "CONTAINER_VERSION("
            << kCurrentContainerVersion << ");\n";
        _Output << "HEADER;\n";
        _Output << "PRODUCT_ID("
            << Quote(Document_.Info.ProductID) << ");\n";
        _Output << "FORMAT_VERSION("
            << Quote(Document_.Info.FormatVersion) << ");\n";
        _Output << "FORMAT_REVISION("
            << Document_.Info.nFormatRevision << ");\n";
        _Output << "PROJECT_ID("
            << Quote(iCAX::Data::to_string(
                Document_.Info.ProjectID)) << ");\n";
        _Output << "MAIN_SCENE_ID("
            << Quote(iCAX::Data::to_string(
                Document_.Info.MainSceneID)) << ");\n";
        _Output << "PROJECT_NAME("
            << Quote(Document_.Info.ProjectName) << ");\n";
        _Output << "PROJECT_SETTINGS("
            << Quote(SerializeObjectMap(
                Document_.Info.ProjectSettings)) << ");\n";
        _Output << "MAIN_SCENE_SETTINGS("
            << Quote(SerializeObjectMap(
                Document_.Info.MainSceneSettings)) << ");\n";
        _Output << "ENDSEC;\n";
        _Output << "DATA;\n";

        for (const auto& _Entity : Document_.Entities)
        {
            _Output << "ENTITY("
                << Quote(iCAX::Data::to_string(
                    _Entity.EntityID)) << ");\n";
        }
        for (const auto& _Component : Document_.Components)
        {
            _Output << "COMPONENT("
                << Quote(iCAX::Data::to_string(
                    _Component.EntityID)) << ','
                << Quote(_Component.ComponentClass) << ','
                << (_Component.bEnabled ? "TRUE" : "FALSE") << ','
                << Quote(SerializeObjectMap(
                    _Component.Properties)) << ");\n";
        }
        for (const auto& _Resource : Document_.Resources)
        {
            _Output << "RESOURCE("
                << Quote(_Resource.Reference.URL) << ','
                << _Resource.Reference.nVersion << ','
                << Quote(_Resource.ResourceTypeID) << ','
                << _Resource.nSchemaVersion << ','
                << (_Resource.Persistence ==
                    EProjectResourcePersistence::Embedded
                    ? "EMBEDDED" : "EXTERNAL") << ','
                << Quote(_Resource.Name) << ','
                << Quote(_Resource.MediaType) << ','
                << Quote(_Resource.FlatBufferIdentifier) << ','
                << Quote(_Resource.ContentHash) << ','
                << Quote(_Resource.Source) << ','
                << _Resource.nSize << ','
                << _Resource.nMinimumReaderVersion << ','
                << _Resource.nFlags << ','
                << Quote(SerializeObjectMap(
                    MetadataToObject(_Resource.Metadata)))
                << ");\n";
        }
        for (const auto& _Resource : Document_.Resources)
        {
            for (const auto& _Dependency : _Resource.Dependencies)
            {
                _Output << "RESOURCE_DEPENDENCY("
                    << Quote(_Resource.Reference.URL) << ','
                    << _Resource.Reference.nVersion << ','
                    << Quote(_Dependency.URL) << ','
                    << _Dependency.nVersion << ");\n";
            }
        }
        for (const auto& _Resource : Document_.Resources)
        {
            if (_Resource.Persistence !=
                EProjectResourcePersistence::Embedded)
            {
                continue;
            }
            _Output << "RESOURCE_BODY("
                << Quote(_Resource.Reference.URL) << ','
                << _Resource.Reference.nVersion << ','
                << Quote(Base64Encode(_Resource.Body)) << ");\n";
        }
        _Output << "ENDSEC;\n";
        _Output << "END-ICAX-PROJECT;\n";
        return _Output.str();
    }

    CProjectFileReadResult DecodeASCII(
        IN std::span<const uint8_t> Bytes_)
    {
        const std::string _Text(
            reinterpret_cast<const char*>(Bytes_.data()),
            Bytes_.size());
        if (_Text.find('\0') != std::string::npos)
        {
            throw std::invalid_argument(
                "ASCII project contains a null byte");
        }
        std::istringstream _Input(_Text);
        std::string _Line;
        size_t _LineNumber = 0;
        auto _ReadLine = [&]() -> bool
        {
            if (!std::getline(_Input, _Line))
            {
                return false;
            }
            ++_LineNumber;
            if (!_Line.empty() && _Line.back() == '\r')
            {
                _Line.pop_back();
            }
            return true;
        };

        CProjectFileReadResult _Result;
        _Result.Encoding = EProjectFileEncoding::ASCII;
        if (!_ReadLine() || _Line.empty())
        {
            throw std::invalid_argument(
                "ASCII project magic is missing");
        }
        _Result.Document.Info.Magic = _Line;
        if (!_ReadLine() || Trim(_Line) != kASCIIIdentifier)
        {
            throw std::invalid_argument(
                "ASCII project identifier is invalid");
        }
        if (!_ReadLine())
        {
            throw std::invalid_argument(
                "ASCII project container version is missing");
        }
        {
            const auto _Record =
                ParseASCIIRecord(_Line, _LineNumber);
            if (_Record.Name != "CONTAINER_VERSION")
            {
                throw std::invalid_argument(
                    "ASCII project container version is missing");
            }
            RequireArgumentCount(_Record, 1, _LineNumber);
            _Result.nContainerVersion = ParseUInt32(
                _Record.Arguments[0],
                "Container version");
            if (_Result.nContainerVersion == 0 ||
                _Result.nContainerVersion >
                    kCurrentContainerVersion)
            {
                throw std::invalid_argument(
                    "Unsupported project container version");
            }
        }
        if (!_ReadLine() || Trim(_Line) != "HEADER;")
        {
            throw std::invalid_argument(
                "ASCII project header section is missing");
        }

        std::set<std::string> _HeaderFields;
        bool _HeaderEnded = false;
        while (_ReadLine())
        {
            if (Trim(_Line).empty())
            {
                continue;
            }
            if (Trim(_Line) == "ENDSEC;")
            {
                _HeaderEnded = true;
                break;
            }
            const auto _Record =
                ParseASCIIRecord(_Line, _LineNumber);
            if (!_HeaderFields.insert(_Record.Name).second)
            {
                throw std::invalid_argument(
                    "Duplicate ASCII project header field: " +
                    _Record.Name);
            }
            RequireArgumentCount(_Record, 1, _LineNumber);
            const auto& _Value = _Record.Arguments[0];
            if (_Record.Name == "PRODUCT_ID")
                _Result.Document.Info.ProductID = _Value;
            else if (_Record.Name == "FORMAT_VERSION")
                _Result.Document.Info.FormatVersion = _Value;
            else if (_Record.Name == "FORMAT_REVISION")
                _Result.Document.Info.nFormatRevision =
                    ParseUInt32(_Value, "Format revision");
            else if (_Record.Name == "PROJECT_ID")
                _Result.Document.Info.ProjectID =
                    ParseUUID(_Value, "Project id");
            else if (_Record.Name == "MAIN_SCENE_ID")
                _Result.Document.Info.MainSceneID =
                    ParseUUID(_Value, "Main scene id");
            else if (_Record.Name == "PROJECT_NAME")
                _Result.Document.Info.ProjectName = _Value;
            else if (_Record.Name == "PROJECT_SETTINGS")
                _Result.Document.Info.ProjectSettings =
                    ParseObjectMap(_Value, "Project settings");
            else if (_Record.Name == "MAIN_SCENE_SETTINGS")
                _Result.Document.Info.MainSceneSettings =
                    ParseObjectMap(_Value, "Main scene settings");
            else
                throw std::invalid_argument(
                    "Unknown ASCII project header field: " +
                    _Record.Name);
        }
        static const std::set<std::string> _RequiredHeaderFields =
        {
            "PRODUCT_ID", "FORMAT_VERSION", "FORMAT_REVISION",
            "PROJECT_ID", "MAIN_SCENE_ID", "PROJECT_NAME",
            "PROJECT_SETTINGS", "MAIN_SCENE_SETTINGS"
        };
        if (!_HeaderEnded || _HeaderFields != _RequiredHeaderFields)
        {
            throw std::invalid_argument(
                "ASCII project header is incomplete");
        }
        if (!_ReadLine() || Trim(_Line) != "DATA;")
        {
            throw std::invalid_argument(
                "ASCII project data section is missing");
        }

        bool _DataEnded = false;
        std::set<CProjectResourceReference> _Bodies;
        while (_ReadLine())
        {
            if (Trim(_Line).empty())
            {
                continue;
            }
            if (Trim(_Line) == "ENDSEC;")
            {
                _DataEnded = true;
                break;
            }
            const auto _Record =
                ParseASCIIRecord(_Line, _LineNumber);
            if (_Record.Name == "ENTITY")
            {
                RequireArgumentCount(_Record, 1, _LineNumber);
                _Result.Document.Entities.push_back({
                    ParseUUID(
                        _Record.Arguments[0],
                        "Entity id")});
            }
            else if (_Record.Name == "COMPONENT")
            {
                RequireArgumentCount(_Record, 4, _LineNumber);
                bool _Enabled = false;
                if (_Record.Arguments[2] == "TRUE")
                    _Enabled = true;
                else if (_Record.Arguments[2] != "FALSE")
                    throw std::invalid_argument(
                        "Component enabled flag is invalid");
                _Result.Document.Components.push_back({
                    ParseUUID(_Record.Arguments[0], "Component entity id"),
                    _Record.Arguments[1],
                    _Enabled,
                    ParseObjectMap(
                        _Record.Arguments[3],
                        "Component properties")});
            }
            else if (_Record.Name == "RESOURCE")
            {
                if (_Record.Arguments.size() != 9 &&
                    _Record.Arguments.size() != 14)
                {
                    throw std::invalid_argument(
                        "RESOURCE at line " +
                        std::to_string(_LineNumber) +
                        " expects 9 legacy or 14 current arguments");
                }
                EProjectResourcePersistence _Persistence;
                if (_Record.Arguments[4] == "EMBEDDED")
                    _Persistence = EProjectResourcePersistence::Embedded;
                else if (_Record.Arguments[4] == "EXTERNAL")
                    _Persistence = EProjectResourcePersistence::External;
                else
                    throw std::invalid_argument(
                        "Resource persistence is invalid");
                CProjectResourceRecord _Resource;
                _Resource.Reference = ParseReference(
                    _Record.Arguments[0],
                    _Record.Arguments[1]);
                _Resource.ResourceTypeID = _Record.Arguments[2];
                _Resource.nSchemaVersion = ParseUInt32(
                    _Record.Arguments[3],
                    "Resource schema version");
                _Resource.Persistence = _Persistence;
                const bool _bCurrent =
                    _Record.Arguments.size() == 14;
                size_t _Index = 5;
                if (_bCurrent)
                {
                    _Resource.Name = _Record.Arguments[_Index++];
                }
                _Resource.MediaType = _Record.Arguments[_Index++];
                if (_bCurrent)
                {
                    _Resource.FlatBufferIdentifier =
                        _Record.Arguments[_Index++];
                }
                _Resource.ContentHash = _Record.Arguments[_Index++];
                _Resource.Source = _Record.Arguments[_Index++];
                if (_bCurrent)
                {
                    _Resource.nSize = ParseUInt64(
                        _Record.Arguments[_Index++],
                        "Resource size");
                    _Resource.nMinimumReaderVersion = ParseUInt32(
                        _Record.Arguments[_Index++],
                        "Resource minimum reader version");
                    _Resource.nFlags = ParseUInt32(
                        _Record.Arguments[_Index++],
                        "Resource flags");
                }
                _Resource.Metadata = ObjectToMetadata(
                    ParseObjectMap(
                        _Record.Arguments[_Index],
                        "Resource metadata"));
                _Result.Document.Resources.push_back(
                    std::move(_Resource));
            }
            else if (_Record.Name == "RESOURCE_DEPENDENCY")
            {
                RequireArgumentCount(_Record, 4, _LineNumber);
                const auto _Parent = ParseReference(
                    _Record.Arguments[0],
                    _Record.Arguments[1]);
                auto* _pResource =
                    _Result.Document.FindResource(_Parent);
                if (!_pResource)
                {
                    throw std::invalid_argument(
                        "Resource dependency precedes or misses its parent");
                }
                _pResource->Dependencies.push_back(ParseReference(
                    _Record.Arguments[2],
                    _Record.Arguments[3]));
            }
            else if (_Record.Name == "RESOURCE_BODY")
            {
                RequireArgumentCount(_Record, 3, _LineNumber);
                const auto _Reference = ParseReference(
                    _Record.Arguments[0],
                    _Record.Arguments[1]);
                auto* _pResource =
                    _Result.Document.FindResource(_Reference);
                if (!_pResource ||
                    !_Bodies.insert(_Reference).second)
                {
                    throw std::invalid_argument(
                        "Resource body precedes, misses, or duplicates its resource");
                }
                _pResource->Body =
                    Base64Decode(_Record.Arguments[2]);
            }
            else
            {
                throw std::invalid_argument(
                    "Unknown ASCII project data record: " +
                    _Record.Name);
            }
        }
        if (!_DataEnded || !_ReadLine() ||
            Trim(_Line) != "END-ICAX-PROJECT;")
        {
            throw std::invalid_argument(
                "ASCII project terminator is missing");
        }
        while (_ReadLine())
        {
            if (!Trim(_Line).empty())
            {
                throw std::invalid_argument(
                    "ASCII project contains trailing data");
            }
        }

        _Result.Document.Canonicalize();
        RequireValidProjectDocument(_Result.Document);
        return _Result;
    }

    class CBinaryWriter final
    {
    public:
        void WriteUInt8(IN const uint8_t Value_)
        {
            m_Bytes.push_back(Value_);
        }

        void WriteUInt32(IN const uint32_t Value_)
        {
            for (uint32_t i = 0; i < 4; ++i)
            {
                m_Bytes.push_back(static_cast<uint8_t>(
                    Value_ >> (i * 8)));
            }
        }

        void WriteUInt64(IN const uint64_t Value_)
        {
            for (uint32_t i = 0; i < 8; ++i)
            {
                m_Bytes.push_back(static_cast<uint8_t>(
                    Value_ >> (i * 8)));
            }
        }

        void WriteBytes(IN std::span<const uint8_t> Bytes_)
        {
            m_Bytes.insert(
                m_Bytes.end(),
                Bytes_.begin(),
                Bytes_.end());
        }

        void WriteString(IN std::string_view Value_)
        {
            WriteUInt64(Value_.size());
            WriteBytes(std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(Value_.data()),
                Value_.size()));
        }

        void WriteUUID(IN const iCAX::Data::uuid& Value_)
        {
            const auto _Bytes = Value_.as_bytes();
            WriteBytes(std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(_Bytes.data()),
                _Bytes.size()));
        }

        void WriteReference(
            IN const CProjectResourceReference& Reference_)
        {
            WriteString(Reference_.URL);
            WriteUInt64(Reference_.nVersion);
        }

        const std::vector<uint8_t>& Bytes() const noexcept
        {
            return m_Bytes;
        }

        std::vector<uint8_t> Take()
        {
            return std::move(m_Bytes);
        }

    private:
        std::vector<uint8_t> m_Bytes;
    };

    class CBinaryReader final
    {
    public:
        explicit CBinaryReader(IN std::span<const uint8_t> Bytes_)
            : m_Bytes(Bytes_)
        {
        }

        uint8_t ReadUInt8()
        {
            Require(1);
            return m_Bytes[m_Position++];
        }

        uint32_t ReadUInt32()
        {
            Require(4);
            uint32_t _Value = 0;
            for (uint32_t i = 0; i < 4; ++i)
            {
                _Value |= static_cast<uint32_t>(
                    m_Bytes[m_Position++]) << (i * 8);
            }
            return _Value;
        }

        uint64_t ReadUInt64()
        {
            Require(8);
            uint64_t _Value = 0;
            for (uint32_t i = 0; i < 8; ++i)
            {
                _Value |= static_cast<uint64_t>(
                    m_Bytes[m_Position++]) << (i * 8);
            }
            return _Value;
        }

        std::span<const uint8_t> ReadBytes(IN const size_t nSize_)
        {
            Require(nSize_);
            const auto _Result = m_Bytes.subspan(
                m_Position,
                nSize_);
            m_Position += nSize_;
            return _Result;
        }

        std::string ReadString()
        {
            const auto _Size = ReadUInt64();
            if (_Size > kMaximumStringSize ||
                _Size > std::numeric_limits<size_t>::max())
            {
                throw std::invalid_argument(
                    "Binary project string is too large");
            }
            const auto _Bytes = ReadBytes(
                static_cast<size_t>(_Size));
            return std::string(
                reinterpret_cast<const char*>(_Bytes.data()),
                _Bytes.size());
        }

        iCAX::Data::uuid ReadUUID()
        {
            const auto _Bytes = ReadBytes(16);
            return iCAX::Data::uuid(
                _Bytes.begin(),
                _Bytes.end());
        }

        CProjectResourceReference ReadReference()
        {
            return {ReadString(), ReadUInt64()};
        }

        bool AtEnd() const noexcept
        {
            return m_Position == m_Bytes.size();
        }

        size_t Remaining() const noexcept
        {
            return m_Bytes.size() - m_Position;
        }

    private:
        void Require(IN const size_t nSize_) const
        {
            if (nSize_ > m_Bytes.size() - m_Position)
            {
                throw std::invalid_argument(
                    "Binary project record is truncated");
            }
        }

        std::span<const uint8_t> m_Bytes;
        size_t m_Position = 0;
    };

    void WriteBinaryRecord(
        IN OUT CBinaryWriter& Writer_,
        IN const EBinaryRecord Type_,
        IN CBinaryWriter Payload_)
    {
        Writer_.WriteUInt8(static_cast<uint8_t>(Type_));
        Writer_.WriteUInt64(Payload_.Bytes().size());
        Writer_.WriteBytes(Payload_.Bytes());
    }

    std::vector<uint8_t> EncodeBinary(
        IN const CProjectDocument& Document_)
    {
        CBinaryWriter _Writer;
        _Writer.WriteBytes(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(
                Document_.Info.Magic.data()),
            Document_.Info.Magic.size()));
        _Writer.WriteUInt8(0);
        _Writer.WriteBytes(kBinaryIdentifier);
        _Writer.WriteUInt32(kCurrentContainerVersion);
        _Writer.WriteString(Document_.Info.ProductID);
        _Writer.WriteString(Document_.Info.FormatVersion);
        _Writer.WriteUInt32(Document_.Info.nFormatRevision);
        _Writer.WriteUUID(Document_.Info.ProjectID);
        _Writer.WriteUUID(Document_.Info.MainSceneID);
        _Writer.WriteString(Document_.Info.ProjectName);
        _Writer.WriteString(SerializeObjectMap(
            Document_.Info.ProjectSettings));
        _Writer.WriteString(SerializeObjectMap(
            Document_.Info.MainSceneSettings));

        uint64_t _RecordCount =
            Document_.Entities.size() +
            Document_.Components.size() +
            Document_.Resources.size();
        for (const auto& _Resource : Document_.Resources)
        {
            _RecordCount += _Resource.Dependencies.size();
            if (_Resource.Persistence ==
                EProjectResourcePersistence::Embedded)
            {
                ++_RecordCount;
            }
        }
        _Writer.WriteUInt64(_RecordCount);

        for (const auto& _Entity : Document_.Entities)
        {
            CBinaryWriter _Payload;
            _Payload.WriteUUID(_Entity.EntityID);
            WriteBinaryRecord(
                _Writer,
                EBinaryRecord::Entity,
                std::move(_Payload));
        }
        for (const auto& _Component : Document_.Components)
        {
            CBinaryWriter _Payload;
            _Payload.WriteUUID(_Component.EntityID);
            _Payload.WriteString(_Component.ComponentClass);
            _Payload.WriteUInt8(_Component.bEnabled ? 1 : 0);
            _Payload.WriteString(SerializeObjectMap(
                _Component.Properties));
            WriteBinaryRecord(
                _Writer,
                EBinaryRecord::Component,
                std::move(_Payload));
        }
        for (const auto& _Resource : Document_.Resources)
        {
            CBinaryWriter _Payload;
            _Payload.WriteReference(_Resource.Reference);
            _Payload.WriteString(_Resource.ResourceTypeID);
            _Payload.WriteUInt32(_Resource.nSchemaVersion);
            _Payload.WriteUInt8(static_cast<uint8_t>(
                _Resource.Persistence));
            _Payload.WriteString(_Resource.Name);
            _Payload.WriteString(_Resource.MediaType);
            _Payload.WriteString(_Resource.FlatBufferIdentifier);
            _Payload.WriteString(_Resource.ContentHash);
            _Payload.WriteString(_Resource.Source);
            _Payload.WriteUInt64(_Resource.nSize);
            _Payload.WriteUInt32(_Resource.nMinimumReaderVersion);
            _Payload.WriteUInt32(_Resource.nFlags);
            _Payload.WriteString(SerializeObjectMap(
                MetadataToObject(_Resource.Metadata)));
            WriteBinaryRecord(
                _Writer,
                EBinaryRecord::Resource,
                std::move(_Payload));
        }
        for (const auto& _Resource : Document_.Resources)
        {
            for (const auto& _Dependency : _Resource.Dependencies)
            {
                CBinaryWriter _Payload;
                _Payload.WriteReference(_Resource.Reference);
                _Payload.WriteReference(_Dependency);
                WriteBinaryRecord(
                    _Writer,
                    EBinaryRecord::ResourceDependency,
                    std::move(_Payload));
            }
        }
        for (const auto& _Resource : Document_.Resources)
        {
            if (_Resource.Persistence !=
                EProjectResourcePersistence::Embedded)
            {
                continue;
            }
            CBinaryWriter _Payload;
            _Payload.WriteReference(_Resource.Reference);
            _Payload.WriteUInt64(_Resource.Body.size());
            _Payload.WriteBytes(_Resource.Body);
            WriteBinaryRecord(
                _Writer,
                EBinaryRecord::ResourceBody,
                std::move(_Payload));
        }
        return _Writer.Take();
    }

    CProjectFileReadResult DecodeBinary(
        IN std::span<const uint8_t> Bytes_,
        IN const size_t nMagicEnd_)
    {
        CProjectFileReadResult _Result;
        _Result.Encoding = EProjectFileEncoding::Binary;
        _Result.Document.Info.Magic = std::string(
            reinterpret_cast<const char*>(Bytes_.data()),
            nMagicEnd_);

        CBinaryReader _Reader(Bytes_.subspan(nMagicEnd_ + 1));
        const auto _Identifier =
            _Reader.ReadBytes(kBinaryIdentifier.size());
        if (!std::equal(
            _Identifier.begin(),
            _Identifier.end(),
            kBinaryIdentifier.begin()))
        {
            throw std::invalid_argument(
                "Binary project identifier is invalid");
        }
        _Result.nContainerVersion = _Reader.ReadUInt32();
        if (_Result.nContainerVersion == 0 ||
            _Result.nContainerVersion > kCurrentContainerVersion)
        {
            throw std::invalid_argument(
                "Unsupported project container version");
        }
        _Result.Document.Info.ProductID = _Reader.ReadString();
        _Result.Document.Info.FormatVersion = _Reader.ReadString();
        _Result.Document.Info.nFormatRevision = _Reader.ReadUInt32();
        _Result.Document.Info.ProjectID = _Reader.ReadUUID();
        _Result.Document.Info.MainSceneID = _Reader.ReadUUID();
        _Result.Document.Info.ProjectName = _Reader.ReadString();
        _Result.Document.Info.ProjectSettings = ParseObjectMap(
            _Reader.ReadString(),
            "Project settings");
        _Result.Document.Info.MainSceneSettings = ParseObjectMap(
            _Reader.ReadString(),
            "Main scene settings");

        const auto _RecordCount = _Reader.ReadUInt64();
        if (_RecordCount > kMaximumRecordCount)
        {
            throw std::invalid_argument(
                "Binary project has too many records");
        }
        std::set<CProjectResourceReference> _Bodies;
        for (uint64_t i = 0; i < _RecordCount; ++i)
        {
            const auto _Type = static_cast<EBinaryRecord>(
                _Reader.ReadUInt8());
            const auto _PayloadSize = _Reader.ReadUInt64();
            if (_PayloadSize > _Reader.Remaining() ||
                _PayloadSize > std::numeric_limits<size_t>::max())
            {
                throw std::invalid_argument(
                    "Binary project payload length is invalid");
            }
            CBinaryReader _Payload(_Reader.ReadBytes(
                static_cast<size_t>(_PayloadSize)));
            switch (_Type)
            {
            case EBinaryRecord::Entity:
                _Result.Document.Entities.push_back(
                    {_Payload.ReadUUID()});
                break;
            case EBinaryRecord::Component:
            {
                CProjectComponentRecord _Component;
                _Component.EntityID = _Payload.ReadUUID();
                _Component.ComponentClass = _Payload.ReadString();
                const auto _Enabled = _Payload.ReadUInt8();
                if (_Enabled > 1)
                {
                    throw std::invalid_argument(
                        "Binary component enabled flag is invalid");
                }
                _Component.bEnabled = _Enabled != 0;
                _Component.Properties = ParseObjectMap(
                    _Payload.ReadString(),
                    "Component properties");
                _Result.Document.Components.push_back(
                    std::move(_Component));
                break;
            }
            case EBinaryRecord::Resource:
            {
                CProjectResourceRecord _Resource;
                _Resource.Reference = _Payload.ReadReference();
                _Resource.ResourceTypeID = _Payload.ReadString();
                _Resource.nSchemaVersion = _Payload.ReadUInt32();
                const auto _Persistence = _Payload.ReadUInt8();
                if (_Persistence != static_cast<uint8_t>(
                        EProjectResourcePersistence::Embedded) &&
                    _Persistence != static_cast<uint8_t>(
                        EProjectResourcePersistence::External))
                {
                    throw std::invalid_argument(
                        "Binary resource persistence is invalid");
                }
                _Resource.Persistence =
                    static_cast<EProjectResourcePersistence>(_Persistence);
                if (_Result.nContainerVersion >= 2)
                {
                    _Resource.Name = _Payload.ReadString();
                }
                _Resource.MediaType = _Payload.ReadString();
                if (_Result.nContainerVersion >= 2)
                {
                    _Resource.FlatBufferIdentifier = _Payload.ReadString();
                }
                _Resource.ContentHash = _Payload.ReadString();
                _Resource.Source = _Payload.ReadString();
                if (_Result.nContainerVersion >= 2)
                {
                    _Resource.nSize = _Payload.ReadUInt64();
                    _Resource.nMinimumReaderVersion = _Payload.ReadUInt32();
                    _Resource.nFlags = _Payload.ReadUInt32();
                }
                _Resource.Metadata = ObjectToMetadata(ParseObjectMap(
                    _Payload.ReadString(),
                    "Resource metadata"));
                _Result.Document.Resources.push_back(
                    std::move(_Resource));
                break;
            }
            case EBinaryRecord::ResourceDependency:
            {
                const auto _Parent = _Payload.ReadReference();
                auto* _pResource =
                    _Result.Document.FindResource(_Parent);
                if (!_pResource)
                {
                    throw std::invalid_argument(
                        "Binary resource dependency precedes or misses its parent");
                }
                _pResource->Dependencies.push_back(
                    _Payload.ReadReference());
                break;
            }
            case EBinaryRecord::ResourceBody:
            {
                const auto _Reference = _Payload.ReadReference();
                auto* _pResource =
                    _Result.Document.FindResource(_Reference);
                if (!_pResource ||
                    !_Bodies.insert(_Reference).second)
                {
                    throw std::invalid_argument(
                        "Binary resource body precedes, misses, or duplicates its resource");
                }
                const auto _Size = _Payload.ReadUInt64();
                if (_Size > _Payload.Remaining() ||
                    _Size > std::numeric_limits<size_t>::max())
                {
                    throw std::invalid_argument(
                        "Binary resource body length is invalid");
                }
                const auto _Body = _Payload.ReadBytes(
                    static_cast<size_t>(_Size));
                _pResource->Body.assign(
                    _Body.begin(),
                    _Body.end());
                break;
            }
            default:
                throw std::invalid_argument(
                    "Unknown binary project record type");
            }
            if (!_Payload.AtEnd())
            {
                throw std::invalid_argument(
                    "Binary project record has trailing payload");
            }
        }
        if (!_Reader.AtEnd())
        {
            throw std::invalid_argument(
                "Binary project contains trailing data");
        }

        _Result.Document.Canonicalize();
        RequireValidProjectDocument(_Result.Document);
        return _Result;
    }

    void WriteAllAndFlush(
        IN const std::filesystem::path& Path_,
        IN std::span<const uint8_t> Bytes_)
    {
        const auto _Handle = ::CreateFileW(
            Path_.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (_Handle == INVALID_HANDLE_VALUE)
        {
            throw std::system_error(
                static_cast<int>(::GetLastError()),
                std::system_category(),
                "Cannot create temporary project file");
        }

        try
        {
            size_t _Position = 0;
            while (_Position < Bytes_.size())
            {
                const auto _Chunk = static_cast<DWORD>(std::min<size_t>(
                    Bytes_.size() - _Position,
                    std::numeric_limits<DWORD>::max()));
                DWORD _Written = 0;
                if (!::WriteFile(
                        _Handle,
                        Bytes_.data() + _Position,
                        _Chunk,
                        &_Written,
                        nullptr) ||
                    _Written != _Chunk)
                {
                    throw std::system_error(
                        static_cast<int>(::GetLastError()),
                        std::system_category(),
                        "Cannot write temporary project file");
                }
                _Position += _Written;
            }
            if (!::FlushFileBuffers(_Handle))
            {
                throw std::system_error(
                    static_cast<int>(::GetLastError()),
                    std::system_category(),
                    "Cannot flush temporary project file");
            }
        }
        catch (...)
        {
            ::CloseHandle(_Handle);
            throw;
        }
        if (!::CloseHandle(_Handle))
        {
            throw std::system_error(
                static_cast<int>(::GetLastError()),
                std::system_category(),
                "Cannot close temporary project file");
        }
    }
}

std::vector<uint8_t> iCAX::ProjectFile::CProjectFileCodec::Encode(
    IN CProjectDocument Document_,
    IN const EProjectFileEncoding Encoding_)
{
    Document_.Canonicalize();
    RequireValidProjectDocument(Document_);
    if (Document_.Info.Magic.find_first_of("\r\n\0", 0, 3) !=
            std::string::npos ||
        Document_.Info.Magic.size() > 4096)
    {
        throw std::invalid_argument(
            "Project magic cannot be represented by the container");
    }

    if (Encoding_ == EProjectFileEncoding::ASCII)
    {
        const auto _Text = EncodeASCII(Document_);
        return std::vector<uint8_t>(
            _Text.begin(),
            _Text.end());
    }
    if (Encoding_ == EProjectFileEncoding::Binary)
    {
        return EncodeBinary(Document_);
    }
    throw std::invalid_argument(
        "Unknown project file encoding");
}

iCAX::ProjectFile::CProjectFileReadResult
iCAX::ProjectFile::CProjectFileCodec::Decode(
    IN std::span<const uint8_t> Bytes_)
{
    if (Bytes_.empty())
    {
        throw std::invalid_argument(
            "Project file is empty");
    }
    const auto _SearchSize = std::min<size_t>(Bytes_.size(), 4097);
    size_t _Delimiter = 0;
    while (_Delimiter < _SearchSize &&
        Bytes_[_Delimiter] != 0 &&
        Bytes_[_Delimiter] != '\n')
    {
        ++_Delimiter;
    }
    if (_Delimiter == 0 || _Delimiter >= _SearchSize)
    {
        throw std::invalid_argument(
            "Project file magic delimiter is missing");
    }
    if (Bytes_[_Delimiter] == '\n')
    {
        return DecodeASCII(Bytes_);
    }
    return DecodeBinary(Bytes_, _Delimiter);
}

iCAX::ProjectFile::CProjectFileReadResult
iCAX::ProjectFile::CProjectFileCodec::Read(
    IN const std::filesystem::path& Path_)
{
    std::ifstream _Input(Path_, std::ios::binary | std::ios::ate);
    if (!_Input)
    {
        throw std::runtime_error(
            "Cannot open project file: " + Path_.string());
    }
    const auto _End = _Input.tellg();
    if (_End < 0 ||
        static_cast<uint64_t>(_End) >
            std::numeric_limits<size_t>::max())
    {
        throw std::runtime_error(
            "Project file size is invalid");
    }
    std::vector<uint8_t> _Bytes(
        static_cast<size_t>(_End));
    _Input.seekg(0, std::ios::beg);
    if (!_Bytes.empty() &&
        !_Input.read(
            reinterpret_cast<char*>(_Bytes.data()),
            static_cast<std::streamsize>(_Bytes.size())))
    {
        throw std::runtime_error(
            "Cannot read complete project file");
    }
    return Decode(_Bytes);
}

void iCAX::ProjectFile::CProjectFileCodec::WriteAtomic(
    IN const std::filesystem::path& Path_,
    IN CProjectDocument Document_,
    IN const EProjectFileEncoding Encoding_)
{
    if (Path_.empty())
    {
        throw std::invalid_argument(
            "Project file path is empty");
    }
    Document_.Canonicalize();
    const auto _Bytes = Encode(Document_, Encoding_);
    const auto _Decoded = Decode(_Bytes);
    if (_Decoded.Document != Document_)
    {
        throw std::runtime_error(
            "Project file self-verification failed");
    }

    auto _Temporary = Path_;
    _Temporary += L".tmp-" + std::wstring(
        iCAX::Data::to_string<wchar_t>(
            iCAX::Data::GenerateNewUUID()));
    try
    {
        WriteAllAndFlush(_Temporary, _Bytes);
        if (!::MoveFileExW(
                _Temporary.c_str(),
                Path_.c_str(),
                MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH))
        {
            throw std::system_error(
                static_cast<int>(::GetLastError()),
                std::system_category(),
                "Cannot atomically replace project file");
        }
    }
    catch (...)
    {
        std::error_code _Ignored;
        std::filesystem::remove(_Temporary, _Ignored);
        throw;
    }
}
