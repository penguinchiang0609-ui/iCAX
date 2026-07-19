#include "pch.h"
#include "VariantSerializer.h"

using namespace iCAX::Data;

/*
* @brief 获取类型名称
* @return std::string
*/
static std::string _GetTypeName(IN const Variant& Value_)
{
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) return "null";
        else if constexpr (std::is_same_v<T, bool>) return "bool";
        else if constexpr (std::is_same_v<T, char>) return "char";
        else if constexpr (std::is_same_v<T, uint8_t>) return "uint8_t";
        else if constexpr (std::is_same_v<T, short>) return "short";
        else if constexpr (std::is_same_v<T, unsigned short>) return "unsigned_short";
        else if constexpr (std::is_same_v<T, int>) return "int";
        else if constexpr (std::is_same_v<T, unsigned int>) return "unsigned_int";
        else if constexpr (std::is_same_v<T, long long>) return "long_long";
        else if constexpr (std::is_same_v<T, unsigned long long>) return "unsigned_long_long";
        else if constexpr (std::is_same_v<T, float>) return "float";
        else if constexpr (std::is_same_v<T, double>) return "double";
        else if constexpr (std::is_same_v<T, std::string>) return "string";
        //else if constexpr (std::is_same_v<T, wchar_t>) return "wstring_char";
        //else if constexpr (std::is_same_v<T, std::wstring>) return "wstring";
        else if constexpr (std::is_same_v<T, uuid>) return "uuid";

        else if constexpr (std::is_same_v<T, Bool1>) return "Bool1";
        else if constexpr (std::is_same_v<T, Bool2>) return "Bool2";
        else if constexpr (std::is_same_v<T, Bool3>) return "Bool3";
        else if constexpr (std::is_same_v<T, Bool4>) return "Bool4";
        else if constexpr (std::is_same_v<T, Bool5>) return "Bool5";
        else if constexpr (std::is_same_v<T, Bool6>) return "Bool6";

        else if constexpr (std::is_same_v<T, Byte1>) return "Byte1";
        else if constexpr (std::is_same_v<T, Byte2>) return "Byte2";
        else if constexpr (std::is_same_v<T, Byte3>) return "Byte3";
        else if constexpr (std::is_same_v<T, Byte4>) return "Byte4";
        else if constexpr (std::is_same_v<T, Byte5>) return "Byte5";
        else if constexpr (std::is_same_v<T, Byte6>) return "Byte6";

        else if constexpr (std::is_same_v<T, Short1>) return "Short1";
        else if constexpr (std::is_same_v<T, Short2>) return "Short2";
        else if constexpr (std::is_same_v<T, Short3>) return "Short3";
        else if constexpr (std::is_same_v<T, Short4>) return "Short4";
        else if constexpr (std::is_same_v<T, Short5>) return "Short5";
        else if constexpr (std::is_same_v<T, Short6>) return "Short6";

        else if constexpr (std::is_same_v<T, Int1>) return "Int1";
        else if constexpr (std::is_same_v<T, Int2>) return "Int2";
        else if constexpr (std::is_same_v<T, Int3>) return "Int3";
        else if constexpr (std::is_same_v<T, Int4>) return "Int4";
        else if constexpr (std::is_same_v<T, Int5>) return "Int5";
        else if constexpr (std::is_same_v<T, Int6>) return "Int6";

        else if constexpr (std::is_same_v<T, Long1>) return "Long1";
        else if constexpr (std::is_same_v<T, Long2>) return "Long2";
        else if constexpr (std::is_same_v<T, Long3>) return "Long3";
        else if constexpr (std::is_same_v<T, Long4>) return "Long4";
        else if constexpr (std::is_same_v<T, Long5>) return "Long5";
        else if constexpr (std::is_same_v<T, Long6>) return "Long6";

        else if constexpr (std::is_same_v<T, Real1>) return "Real1";
        else if constexpr (std::is_same_v<T, Real2>) return "Real2";
        else if constexpr (std::is_same_v<T, Real3>) return "Real3";
        else if constexpr (std::is_same_v<T, Real4>) return "Real4";
        else if constexpr (std::is_same_v<T, Real5>) return "Real5";
        else if constexpr (std::is_same_v<T, Real6>) return "Real6";

        else if constexpr (std::is_same_v<T, Double1>) return "Double1";
        else if constexpr (std::is_same_v<T, Double2>) return "Double2";
        else if constexpr (std::is_same_v<T, Double3>) return "Double3";
        else if constexpr (std::is_same_v<T, Double4>) return "Double4";
        else if constexpr (std::is_same_v<T, Double5>) return "Double5";
        else if constexpr (std::is_same_v<T, Double6>) return "Double6";

        else if constexpr (std::is_same_v<T, Bool2x2>) return "Bool2x2";
        else if constexpr (std::is_same_v<T, Bool3x3>) return "Bool3x3";
        else if constexpr (std::is_same_v<T, Bool4x4>) return "Bool4x4";

        else if constexpr (std::is_same_v<T, Byte2x2>) return "Byte2x2";
        else if constexpr (std::is_same_v<T, Byte3x3>) return "Byte3x3";
        else if constexpr (std::is_same_v<T, Byte4x4>) return "Byte4x4";

        else if constexpr (std::is_same_v<T, Short2x2>) return "Short2x2";
        else if constexpr (std::is_same_v<T, Short3x3>) return "Short3x3";
        else if constexpr (std::is_same_v<T, Short4x4>) return "Short4x4";

        else if constexpr (std::is_same_v<T, Int2x2>) return "Int2x2";
        else if constexpr (std::is_same_v<T, Int3x3>) return "Int3x3";
        else if constexpr (std::is_same_v<T, Int4x4>) return "Int4x4";

        else if constexpr (std::is_same_v<T, Long2x2>) return "Long2x2";
        else if constexpr (std::is_same_v<T, Long3x3>) return "Long3x3";
        else if constexpr (std::is_same_v<T, Long4x4>) return "Long4x4";

        else if constexpr (std::is_same_v<T, Real2x2>) return "Real2x2";
        else if constexpr (std::is_same_v<T, Real3x3>) return "Real3x3";
        else if constexpr (std::is_same_v<T, Real4x4>) return "Real4x4";

        else if constexpr (std::is_same_v<T, Double2x2>) return "Double2x2";
        else if constexpr (std::is_same_v<T, Double3x3>) return "Double3x3";
        else if constexpr (std::is_same_v<T, Double4x4>) return "Double4x4";

        else if constexpr (std::is_same_v<T, ObjectMap>) return "Object";
        else if constexpr (std::is_same_v<T, VariantArray>) return "Array";
        else return "unknown";
        }, Value_.m_Value);

}


//// 辅助函数
//static std::string _WStringToUtf8(const std::wstring& ws)
//{
//    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
//    return conv.to_bytes(ws);
//}
//
//static std::wstring _Utf8ToWString(const std::string& s)
//{
//    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
//    return conv.from_bytes(s);
//}

static void _WriteEscaped(std::ostringstream& oss, const std::string& str)
{
    for (char c : str)
    {
        switch (c)
        {
        case '\"': oss << "\\\""; break;
        case '\\': oss << "\\\\"; break;
        case '\b': oss << "\\b"; break;
        case '\f': oss << "\\f"; break;
        case '\n': oss << "\\n"; break;
        case '\r': oss << "\\r"; break;
        case '\t': oss << "\\t"; break;
        default: oss << c; break;
        }
    }
}

static void _SkipWhitespace(const std::string& str, size_t& pos)
{
    while (pos < str.size() && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
}

static std::string _ReadEscaped(const std::string& str, size_t& pos)
{
    std::ostringstream oss;
    if (pos >= str.size()) throw std::runtime_error("Expected string");
    if (str[pos] != '\"') throw std::runtime_error("Expected '\"'");
    ++pos;
    while (pos < str.size() && str[pos] != '\"')
    {
        if (str[pos] == '\\')
        {
            ++pos;
            if (pos >= str.size()) throw std::runtime_error("Invalid escape sequence");
            switch (str[pos])
            {
            case '\"': oss << '\"'; break;
            case '\\': oss << '\\'; break;
            case 'b': oss << '\b'; break;
            case 'f': oss << '\f'; break;
            case 'n': oss << '\n'; break;
            case 'r': oss << '\r'; break;
            case 't': oss << '\t'; break;
            default: oss << str[pos]; break;
            }
        }
        else
        {
            oss << str[pos];
        }
        ++pos;
    }
    if (pos >= str.size() || str[pos] != '\"') throw std::runtime_error("Expected closing '\"'");
    ++pos;
    return oss.str();
}

static  std::string _ParseValueString(const std::string& str, size_t& pos)
{
    size_t start = pos;
    while (pos < str.size() &&
        str[pos] != ',' &&
        str[pos] != '}' &&
        str[pos] != ']' &&
        !std::isspace(static_cast<unsigned char>(str[pos])))
    {
        ++pos;
    }
    return str.substr(start, pos - start);
}

template<typename T>
std::vector<T> _ParseVector(const std::string& str, size_t& pos, size_t dim, std::function<T(const std::string&)> convert)
{
    if (pos >= str.size() || str[pos] != '[')
        throw std::runtime_error("Expected '['");
    ++pos;

    std::vector<T> tmp;
    tmp.reserve(dim);
    while (true)
    {
        _SkipWhitespace(str, pos);
        if (pos >= str.size()) throw std::runtime_error("Expected ']'");
        if (str[pos] == ']') { ++pos; break; }

        std::string s = _ParseValueString(str, pos);
        tmp.push_back(convert(s));

        _SkipWhitespace(str, pos);
        if (pos < str.size() && str[pos] == ',') ++pos;
    }
    if (tmp.size() != dim)
        throw std::runtime_error("Dimension mismatch, expected " + std::to_string(dim));
    return tmp;
}

static void _Dump(IN std::ostringstream& oss, IN const Variant& var)

{
    std::visit([&](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            oss << "{\"__variant_type\":\"" << _GetTypeName(var) << "\",\"value\":";
            if constexpr (std::is_same_v<T, std::monostate>) oss << "null";
            else if constexpr (std::is_same_v<T, bool>) oss << (arg ? "true" : "false");
            else if constexpr (std::is_same_v<T, char>)
            {
                oss << static_cast<int>(arg);
            }
            else if constexpr (std::is_same_v<T, uint8_t>)
            {
                oss << static_cast<unsigned int>(arg);
            }
            else if constexpr (std::is_same_v<T, short>
                || std::is_same_v<T, unsigned short>
                || std::is_same_v<T, int>
                || std::is_same_v<T, unsigned int>
                || std::is_same_v<T, long long>
                || std::is_same_v<T, unsigned long long>
                || std::is_same_v<T, float>
                || std::is_same_v<T, double>)
            {
                oss << arg;
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                oss << "\"";
                _WriteEscaped(oss, arg);
                oss << "\"";
            }
            //else if constexpr (std::is_same_v<T, wchar_t>)
            //{
            //    std::wstring ws(1, arg);
            //    std::string utf8 = _WStringToUtf8(ws);
            //    oss << "\"";
            //    _WriteEscaped(oss, utf8);
            //    oss << "\"";
            //}
            //else if constexpr (std::is_same_v<T, std::wstring>)
            //{
            //    std::string utf8 = _WStringToUtf8(arg);
            //    oss << "\"";
            //    _WriteEscaped(oss, utf8);
            //    oss << "\"";
            //}
            else if constexpr (std::is_same_v<T, uuid>)
            {
                oss << "\"";
                _WriteEscaped(oss, to_string(arg));
                oss << "\"";
            }
            else if constexpr (std::is_same_v<T, Bool1>
                || std::is_same_v<T, Bool2>
                || std::is_same_v<T, Bool3>
                || std::is_same_v<T, Bool4>
                || std::is_same_v<T, Bool5>
                || std::is_same_v<T, Bool6>
                || std::is_same_v<T, Byte1>
                || std::is_same_v<T, Byte2>
                || std::is_same_v<T, Byte3>
                || std::is_same_v<T, Byte4>
                || std::is_same_v<T, Byte5>
                || std::is_same_v<T, Byte6>
                || std::is_same_v<T, Short1>
                || std::is_same_v<T, Short2>
                || std::is_same_v<T, Short3>
                || std::is_same_v<T, Short4>
                || std::is_same_v<T, Short5>
                || std::is_same_v<T, Short6>
                || std::is_same_v<T, Int1>
                || std::is_same_v<T, Int2>
                || std::is_same_v<T, Int3>
                || std::is_same_v<T, Int4>
                || std::is_same_v<T, Int5>
                || std::is_same_v<T, Int6>
                || std::is_same_v<T, Long1>
                || std::is_same_v<T, Long2>
                || std::is_same_v<T, Long3>
                || std::is_same_v<T, Long4>
                || std::is_same_v<T, Long5>
                || std::is_same_v<T, Long6>
                || std::is_same_v<T, Real1>
                || std::is_same_v<T, Real2>
                || std::is_same_v<T, Real3>
                || std::is_same_v<T, Real4>
                || std::is_same_v<T, Real5>
                || std::is_same_v<T, Real6>
                || std::is_same_v<T, Double1>
                || std::is_same_v<T, Double2>
                || std::is_same_v<T, Double3>
                || std::is_same_v<T, Double4>
                || std::is_same_v<T, Double5>
                || std::is_same_v<T, Double6>)
            {
                oss << "[";
                for (int i = 0; i < (int)arg.size(); ++i)
                {
                    if (i > 0) oss << ",";
                    oss << arg[i];
                }
                oss << "]";
            }
            else if constexpr (std::is_same_v<T, Bool2x2>
                || std::is_same_v<T, Bool3x3>
                || std::is_same_v<T, Bool4x4>
                || std::is_same_v<T, Byte2x2>
                || std::is_same_v<T, Byte3x3>
                || std::is_same_v<T, Byte4x4>
                || std::is_same_v<T, Short2x2>
                || std::is_same_v<T, Short3x3>
                || std::is_same_v<T, Short4x4>
                || std::is_same_v<T, Int2x2>
                || std::is_same_v<T, Int3x3>
                || std::is_same_v<T, Int4x4>
                || std::is_same_v<T, Long2x2>
                || std::is_same_v<T, Long3x3>
                || std::is_same_v<T, Long4x4>
                || std::is_same_v<T, Real2x2>
                || std::is_same_v<T, Real3x3>
                || std::is_same_v<T, Real4x4>
                || std::is_same_v<T, Double2x2>
                || std::is_same_v<T, Double3x3>
                || std::is_same_v<T, Double4x4>)
            {
                oss << "[";
                bool bFirst = true;
                for (int i = 0; i < arg.Rows(); ++i)
                {
                    for (int j = 0; j < arg.Cols(); ++j)
                    {
                        if (!bFirst)
                        {
                            oss << ",";
                        }
                        oss << arg(i, j);
                        bFirst = false;
                    }
                }
                oss << "]";
            }

            else if constexpr (std::is_same_v<T, ObjectMap>)
            {
                oss << "{";
                bool first = true;
                for (auto& [k, v] : arg)
                {
                    if (!first) oss << ",";
                    first = false;
                    oss << "\"";
                    _WriteEscaped(oss, k);
                    oss << "\":";
                    _Dump(oss, v);
                }
                oss << "}";
            }
            else if constexpr (std::is_same_v<T, VariantArray>)
            {
                oss << "[";
                for (size_t i = 0; i < arg.size(); ++i)
                {
                    if (i > 0) oss << ",";
                    _Dump(oss, arg[i]);
                }
                oss << "]";
            }
            else
            {
                oss << "unknown";
            }
            oss << "}";
        }, var.m_Value);
}

static Variant _Parse(const std::string& str, size_t& pos)
{
    _SkipWhitespace(str, pos);
    if (pos >= str.size() || str[pos] != '{') throw std::runtime_error("Expected '{'");
    ++pos;

    std::string type;
    Variant result;

    while (pos < str.size() && str[pos] != '}')
    {
        _SkipWhitespace(str, pos);
        std::string key = _ReadEscaped(str, pos);
        _SkipWhitespace(str, pos);
        if (str[pos] != ':') throw std::runtime_error("Expected ':'");
        ++pos;
        _SkipWhitespace(str, pos);

        if (key == "__variant_type")
        {
            type = _ReadEscaped(str, pos);
        }
        else if (key == "value")
        {
            if (type.empty()) throw std::runtime_error("Variant type missing");

            if (type == "null") { result = Variant(); if (str[pos] == 'n') pos += 4; }
            else if (type == "bool") { std::string s = _ParseValueString(str, pos); result = s == "true"; }
            else if (type == "char") { std::string s = _ParseValueString(str, pos); result = static_cast<char>(std::stoi(s)); }
            else if (type == "uint8_t") { std::string s = _ParseValueString(str, pos); result = static_cast<uint8_t>(std::stoi(s)); }
            else if (type == "short") { std::string s = _ParseValueString(str, pos); result = static_cast<short>(std::stoi(s)); }
            else if (type == "unsigned_short") { std::string s = _ParseValueString(str, pos); result = static_cast<unsigned short>(std::stoi(s)); }
            else if (type == "int") { std::string s = _ParseValueString(str, pos); result = std::stoi(s); }
            else if (type == "unsigned_int") { std::string s = _ParseValueString(str, pos); result = static_cast<unsigned int>(std::stoi(s)); }
            else if (type == "long_long") { std::string s = _ParseValueString(str, pos); result = std::stoll(s); }
            else if (type == "unsigned_long_long") { std::string s = _ParseValueString(str, pos); result = std::stoull(s); }
            else if (type == "float") { std::string s = _ParseValueString(str, pos); result = std::stof(s); }
            else if (type == "double") { std::string s = _ParseValueString(str, pos); result = std::stod(s); }
            else if (type == "string") { result = Variant(_ReadEscaped(str, pos)); }
            //else if (type == "wstring") { std::string s = _ReadEscaped(str, pos); result = _Utf8ToWString(s); }
            //else if (type == "wstring_char") { std::string s = _ReadEscaped(str, pos); result = _Utf8ToWString(s)[0]; }
            else if (type == "uuid")
            {
                std::string s = _ReadEscaped(str, pos);
                auto parsed = uuid::from_string(s);
                if (!parsed.has_value())
                {
                    throw std::runtime_error("Invalid uuid: " + s);
                }

                result = Variant(*parsed);
            }
            else if (type == "Array")
            {
                if (str[pos] != '[') throw std::runtime_error("Expected '['");
                ++pos;
                VariantArray arr;
                while (pos < str.size() && str[pos] != ']')
                {
                    _SkipWhitespace(str, pos);
                    arr.push_back(_Parse(str, pos));
                    _SkipWhitespace(str, pos);
                    if (str[pos] == ',') ++pos;
                }
                if (pos >= str.size() || str[pos] != ']') throw std::runtime_error("Expected ']'");
                ++pos;
                result = Variant(arr);
            }
            else if (type == "Object")
            {
                if (str[pos] != '{') throw std::runtime_error("Expected '{'");
                ++pos;
                ObjectMap obj;
                while (pos < str.size() && str[pos] != '}')
                {
                    _SkipWhitespace(str, pos);
                    std::string k = _ReadEscaped(str, pos);
                    _SkipWhitespace(str, pos);
                    if (str[pos] != ':') throw std::runtime_error("Expected ':'");
                    ++pos;
                    _SkipWhitespace(str, pos);
                    Variant v = _Parse(str, pos);
                    obj[k] = v;
                    _SkipWhitespace(str, pos);
                    if (str[pos] == ',') ++pos;
                }
                if (pos >= str.size() || str[pos] != '}') throw std::runtime_error("Expected '}'");
                ++pos;
                result = Variant(obj);
            }
            else if (type == "Bool1" || type == "Bool2" || type == "Bool3" || type == "Bool4" || type == "Bool5" || type == "Bool6")
            {
                size_t dim = std::stoul(type.substr(4));
                auto tmp = _ParseVector<bool>(str, pos, dim, [](const std::string& _s)
                {
                    return _s == "true" || _s == "1";
                });

                switch (dim)
                {
                case 1: result = Variant(Bool1(tmp)); break;
                case 2: result = Variant(Bool2(tmp)); break;
                case 3: result = Variant(Bool3(tmp)); break;
                case 4: result = Variant(Bool4(tmp)); break;
                case 5: result = Variant(Bool5(tmp)); break;
                case 6: result = Variant(Bool6(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Byte1" || type == "Byte2" || type == "Byte3" || type == "Byte4" || type == "Byte5" || type == "Byte6")
            {
                size_t dim = std::stoul(type.substr(4));
                auto tmp = _ParseVector<unsigned char>(str, pos, dim, [](const std::string& _s)
                    {
                        return static_cast<unsigned char>(std::stoi(_s));
                    });
                switch (dim)
                {
                case 1: result = Variant(Byte1(tmp)); break;
                case 2: result = Variant(Byte2(tmp)); break;
                case 3: result = Variant(Byte3(tmp)); break;
                case 4: result = Variant(Byte4(tmp)); break;
                case 5: result = Variant(Byte5(tmp)); break;
                case 6: result = Variant(Byte6(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Short1" || type == "Short2" || type == "Short3" || type == "Short4" || type == "Short5" || type == "Short6")
            {
                size_t dim = std::stoul(type.substr(5));
                auto tmp = _ParseVector<short>(str, pos, dim, [](const std::string& _s)
                    {
                        return static_cast<short>(std::stoi(_s));
                    });

                switch (dim)
                {
                case 1: result = Variant(Short1(tmp)); break;
                case 2: result = Variant(Short2(tmp)); break;
                case 3: result = Variant(Short3(tmp)); break;
                case 4: result = Variant(Short4(tmp)); break;
                case 5: result = Variant(Short5(tmp)); break;
                case 6: result = Variant(Short6(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Int1" || type == "Int2" || type == "Int3" || type == "Int4" || type == "Int5" || type == "Int6")
            {
                size_t dim = std::stoul(type.substr(3));
                auto tmp = _ParseVector<int>(str, pos, dim, [](const std::string& _s)
                    {
                        return std::stoi(_s);
                    });

                switch (dim)
                {
                case 1: result = Variant(Int1(tmp)); break;
                case 2: result = Variant(Int2(tmp)); break;
                case 3: result = Variant(Int3(tmp)); break;
                case 4: result = Variant(Int4(tmp)); break;
                case 5: result = Variant(Int5(tmp)); break;
                case 6: result = Variant(Int6(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Long1" || type == "Long2" || type == "Long3" || type == "Long4" || type == "Long5" || type == "Long6")
            {
                size_t dim = std::stoul(type.substr(4));
                auto tmp = _ParseVector<long long>(str, pos, dim, [](const std::string& _s)
                    {
                        return std::stoll(_s);
                    });

                switch (dim)
                {
                case 1: result = Variant(Long1(tmp)); break;
                case 2: result = Variant(Long2(tmp)); break;
                case 3: result = Variant(Long3(tmp)); break;
                case 4: result = Variant(Long4(tmp)); break;
                case 5: result = Variant(Long5(tmp)); break;
                case 6: result = Variant(Long6(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Real1" || type == "Real2" || type == "Real3" || type == "Real4" || type == "Real5" || type == "Real6")
            {
                size_t dim = std::stoul(type.substr(4));
                auto tmp = _ParseVector<float>(str, pos, dim, [](const std::string& _s)
                    {
                        return std::stof(_s);
                    });

                switch (dim)
                {
                case 1: result = Variant(Real1(tmp)); break;
                case 2: result = Variant(Real2(tmp)); break;
                case 3: result = Variant(Real3(tmp)); break;
                case 4: result = Variant(Real4(tmp)); break;
                case 5: result = Variant(Real5(tmp)); break;
                case 6: result = Variant(Real6(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Double1" || type == "Double2" || type == "Double3" || type == "Double4" || type == "Double5" || type == "Double6")
            {
                size_t dim = std::stoul(type.substr(6));
                auto tmp = _ParseVector<double>(str, pos, dim, [](const std::string& _s)
                    {
                        return std::stod(_s);
                    });

                switch (dim)
                {
                case 1: result = Variant(Double1(tmp)); break;
                case 2: result = Variant(Double2(tmp)); break;
                case 3: result = Variant(Double3(tmp)); break;
                case 4: result = Variant(Double4(tmp)); break;
                case 5: result = Variant(Double5(tmp)); break;
                case 6: result = Variant(Double6(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Bool2x2" || type == "Bool3x3" || type == "Bool4x4")
            {
                size_t dim = std::stoul(type.substr(6));
                dim = dim * dim;
                auto tmp = _ParseVector<bool>(str, pos, dim, [](const std::string& _s)
                    {
                        return _s == "true" || _s == "1";
                    });

                switch (dim)
                {
                case 4: result = Variant(Bool2x2(tmp)); break;
                case 9: result = Variant(Bool3x3(tmp)); break;
                case 16: result = Variant(Bool4x4(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Byte2x2" || type == "Byte3x3" || type == "Byte4x4")
            {
                size_t dim = std::stoul(type.substr(6));
                dim = dim * dim;
                auto tmp = _ParseVector<unsigned char>(str, pos, dim, [](const std::string& _s)
                    {
                        return static_cast<unsigned char>(std::stoi(_s));
                    });

                switch (dim)
                {
                case 4: result = Variant(Byte2x2(tmp)); break;
                case 9: result = Variant(Byte3x3(tmp)); break;
                case 16: result = Variant(Byte4x4(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Short2x2" || type == "Short3x3" || type == "Short4x4")
            {
                size_t dim = std::stoul(type.substr(7));
                dim = dim * dim;
                auto tmp = _ParseVector<short>(str, pos, dim, [](const std::string& _s)
                    {
                        return static_cast<short>(std::stoi(_s));
                    });

                switch (dim)
                {
                case 4: result = Variant(Short2x2(tmp)); break;
                case 9: result = Variant(Short3x3(tmp)); break;
                case 16: result = Variant(Short4x4(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Int2x2" || type == "Int3x3" || type == "Int4x4")
            {
                size_t dim = std::stoul(type.substr(5));
                dim = dim * dim;
                auto tmp = _ParseVector<int>(str, pos, dim, [](const std::string& _s)
                    {
                        return std::stoi(_s);
                    });

                switch (dim)
                {
                case 4: result = Variant(Int2x2(tmp)); break;
                case 9: result = Variant(Int3x3(tmp)); break;
                case 16: result = Variant(Int4x4(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Long2x2" || type == "Long3x3" || type == "Long4x4")
            {
                size_t dim = std::stoul(type.substr(6));
                dim = dim * dim;
                auto tmp = _ParseVector<long long>(str, pos, dim, [](const std::string& _s)
                    {
                        return std::stoll(_s);
                    });

                switch (dim)
                {
                case 4: result = Variant(Long2x2(tmp)); break;
                case 9: result = Variant(Long3x3(tmp)); break;
                case 16: result = Variant(Long4x4(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Real2x2" || type == "Real3x3" || type == "Real4x4")
            {
                size_t dim = std::stoul(type.substr(6));
                dim = dim * dim;
                auto tmp = _ParseVector<float>(str, pos, dim, [](const std::string& _s)
                    {
                        return std::stof(_s);
                    });

                switch (dim)
                {
                case 4: result = Variant(Real2x2(tmp)); break;
                case 9: result = Variant(Real3x3(tmp)); break;
                case 16: result = Variant(Real4x4(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else if (type == "Double2x2" || type == "Double3x3" || type == "Double4x4")
            {
                size_t dim = std::stoul(type.substr(6));
                dim = dim * dim;
                auto tmp = _ParseVector<double>(str, pos, dim, [](const std::string& _s)
                    {
                        return std::stod(_s);
                    });

                switch (dim)
                {
                case 4: result = Variant(Double2x2(tmp)); break;
                case 9: result = Variant(Double3x3(tmp)); break;
                case 16: result = Variant(Double4x4(tmp)); break;
                default: throw std::runtime_error("Unsupported Bool dimension: " + type);
                }
            }
            else
            {
                // TODO: 支持 Array1/Array2 数组类型
                throw std::runtime_error("Unsupported type: " + type);
            }
        }

        _SkipWhitespace(str, pos);
        if (pos < str.size() && str[pos] == ',') ++pos;
    }

    if (pos >= str.size() || str[pos] != '}') throw std::runtime_error("Expected '}'");
    ++pos;

    return result;
}

std::string VariantSerializer::Serialize(IN const Variant& var)
{
    std::ostringstream oss;
    _Dump(oss, var);
    return oss.str();
}

// 反序列化（简化版本，可后续补充完整）
Variant VariantSerializer::Deserialize(IN const std::string& json)
{
    size_t pos = 0;
    return _Parse(json, pos);
}
