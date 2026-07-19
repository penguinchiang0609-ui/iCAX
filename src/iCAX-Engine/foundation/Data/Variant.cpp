#include "pch.h"

#include "Variant.h"

/*
* @brief 路径Token
*/
struct PathToken
{
    enum class Type { Key, Index };
    Type type;
    std::string key;   // type == Key 时有效
    size_t index = 0;  // type == Index 时有效
};

/*
* @brief 解析路径
* @param [in] strPath_
*/
std::vector<PathToken> ParsePath(IN const std::string& strPath_)
{
    std::vector<PathToken> _Result;
    std::string _strBuffer;
    size_t i = 0;

    auto pushKey = [&]()
        {
            if (!_strBuffer.empty())
            {
                _Result.push_back({ PathToken::Type::Key, _strBuffer });
                _strBuffer.clear();
            }
        };

    while (i < strPath_.length())
    {
        char ch = strPath_[i];
        if (ch == '.')
        {
            pushKey();
            ++i;
        }
        else if (ch == '[')
        {
            pushKey();
            ++i;
            std::string _strIndex;
            while (i < strPath_.length() && strPath_[i] != ']')
            {
                _strIndex += strPath_[i++];
            }
            if (i < strPath_.length() && strPath_[i] == ']')
            {
                ++i;
            }
            size_t _nIndex = static_cast<size_t>(std::stoul(_strIndex));
            _Result.push_back({ PathToken::Type::Index, "", _nIndex });
        }
        else
        {
            _strBuffer += ch;
            ++i;
        }
    }
    pushKey();
    return _Result;
}

const iCAX::Data::Variant iCAX::Data::Variant::Nil = iCAX::Data::Variant();

//!< 默认构造函数
iCAX::Data::Variant::Variant()
    : m_Value(std::monostate{})
{
}

//!< 根据路径寻找叶节点值
std::optional<iCAX::Data::Variant> iCAX::Data::Variant::GetByPath(IN const std::string& strPath_) const
{
    if (strPath_ == "." || strPath_.empty())
    {
        return *this;
    }
    auto _vecToken = ParsePath(strPath_);
    const Variant* _Current = this;

    for (const auto& _Token : _vecToken)
    {
        if (_Token.type == PathToken::Type::Key)
        {
            if (!_Current->Is<ObjectMap>())
            {
                return std::nullopt;
            }

            const auto& _Map = std::get<ObjectMap>(_Current->m_Value);
            auto _Ite = _Map.find(_Token.key);
            if (_Ite == _Map.end())
            {
                return std::nullopt;
            }

            _Current = &_Ite->second;
        }
        else if (_Token.type == PathToken::Type::Index)
        {
            if (!_Current->Is<VariantArray>())
            {
                return std::nullopt;
            }

            const auto& _Array = std::get<VariantArray>(_Current->m_Value);
            if (_Token.index >= _Array.size())
            {
                return std::nullopt;
            }

            auto _Ite = _Array.begin();
            std::advance(_Ite, _Token.index);
            _Current = &(*_Ite);
        }
    }

    return *_Current;
}

//!< 根据路径设置值
void  iCAX::Data::Variant::SetByPath(IN const std::string& strPath_, IN const  iCAX::Data::Variant& Value_)
{
    if (strPath_ == "." || strPath_.empty())
    {
        this->operator=(Value_);
        return;
    }

    auto _vecTokens = ParsePath(strPath_);
    Variant* _Current = this;

    for (size_t i = 0; i < _vecTokens.size(); ++i)
    {
        const auto& _Token = _vecTokens[i];
        bool _bIsLast = (i == _vecTokens.size() - 1);

        if (_Token.type == PathToken::Type::Key)
        {
            if (!_Current->Is<ObjectMap>())
                *_Current = ObjectMap{};

            auto& _Map = std::get<ObjectMap>(_Current->m_Value);
            if (_bIsLast)
            {
                _Map[_Token.key] = Value_;
                return;
            }
            else
            {
                _Current = &_Map[_Token.key];
            }
        }
        else if (_Token.type == PathToken::Type::Index)
        {
            if (!_Current->Is<VariantArray>())
                *_Current = VariantArray{};

            auto& _Array = std::get<VariantArray>(_Current->m_Value);
            if (_Array.size() <= _Token.index)
            {
                _Array.resize(_Token.index + 1); // 自动扩展
            }

            if (_bIsLast)
            {
                auto it = _Array.begin();
                std::advance(it, _Token.index);
                *it = Value_;
                return;
            }
            else
            {
                auto _Ite = _Array.begin();
                std::advance(_Ite, _Token.index);
                _Current = &(*_Ite);
            }
        }
    }
}

//!< 筛选
iCAX::Data::VariantArray iCAX::Data::Variant::FilterByPathAndPredicate(IN const std::string& strPath_, IN std::function<bool(const Variant&)> Predicate_) const
{
    VariantArray result;

    if (!Is<VariantArray>())
        return result;

    const auto& array = std::get<VariantArray>(m_Value);
    for (const auto& item : array)
    {
        auto maybe = item.GetByPath(strPath_);
        if (maybe.has_value() && Predicate_(maybe.value()))
        {
            result.push_back(item);
        }
    }

    return result;
}

bool iCAX::Data::Variant::operator==(const Variant& rhs) const
{
    return m_Value == rhs.m_Value;
}

bool iCAX::Data::Variant::operator!=(const Variant& rhs) const
{
    return !(*this == rhs);
}

bool iCAX::Data::Variant::operator<(const Variant& rhs) const
{
    return m_Value < rhs.m_Value;
}

bool iCAX::Data::Variant::operator>(const Variant& Right_) const
{
    return Right_ < *this;
}

bool iCAX::Data::Variant::operator<=(const Variant& Right_) const
{
    return !(*this > Right_);
}

bool iCAX::Data::Variant::operator>=(const Variant& Right_) const
{
    return !(*this < Right_);
}
