#include "pch.h"

#include "EntityLanguage.h"

#include "Data/Variant.h"

#include <limits>
#include <sstream>

namespace
{
    using namespace iCAX::Data;
    using namespace iCAX::Database;
    using namespace iCAX::DatabaseLanguage;

    enum class ETokenKind
    {
        Identifier,
        String,
        Number,
        Symbol,
        End,
    };

    struct SToken final
    {
        ETokenKind Kind = ETokenKind::End;
        std::string Text;
        size_t Position = 0;
    };

    std::string Upper(IN std::string_view Text_)
    {
        std::string _Result(Text_);
        std::transform(
            _Result.begin(),
            _Result.end(),
            _Result.begin(),
            [](IN const unsigned char Character_)
            {
                return static_cast<char>(std::toupper(Character_));
            });
        return _Result;
    }

    class CLexer final
    {
    public:
        explicit CLexer(IN std::string_view Text_)
            : m_Text(Text_)
        {
        }

        std::vector<SToken> Tokenize()
        {
            std::vector<SToken> _Tokens;
            while (true)
            {
                SkipTrivia();
                if (m_Position >= m_Text.size())
                {
                    _Tokens.push_back({
                        ETokenKind::End,
                        {},
                        m_Position,
                    });
                    return _Tokens;
                }

                const auto _Start = m_Position;
                const char _Character = m_Text[m_Position];
                if (std::isalpha(static_cast<unsigned char>(_Character))
                    || _Character == '_')
                {
                    ++m_Position;
                    while (m_Position < m_Text.size())
                    {
                        const char _Next = m_Text[m_Position];
                        if (!std::isalnum(static_cast<unsigned char>(_Next))
                            && _Next != '_')
                        {
                            break;
                        }
                        ++m_Position;
                    }
                    _Tokens.push_back({
                        ETokenKind::Identifier,
                        std::string(m_Text.substr(
                            _Start,
                            m_Position - _Start)),
                        _Start,
                    });
                    continue;
                }

                if (_Character == '\'' || _Character == '"')
                {
                    _Tokens.push_back(ReadString());
                    continue;
                }

                if (std::isdigit(static_cast<unsigned char>(_Character))
                    || (_Character == '-'
                        && m_Position + 1 < m_Text.size()
                        && std::isdigit(static_cast<unsigned char>(
                            m_Text[m_Position + 1]))))
                {
                    _Tokens.push_back(ReadNumber());
                    continue;
                }

                const auto _Two = m_Position + 1 < m_Text.size()
                    ? m_Text.substr(m_Position, 2)
                    : std::string_view();
                if (_Two == "=>"
                    || _Two == "=="
                    || _Two == "!="
                    || _Two == "<="
                    || _Two == ">="
                    || _Two == "&&"
                    || _Two == "||"
                    || _Two == "<>")
                {
                    m_Position += 2;
                    _Tokens.push_back({
                        ETokenKind::Symbol,
                        std::string(_Two),
                        _Start,
                    });
                    continue;
                }

                if (std::string_view("(){}[];,.=!<>:+").find(_Character)
                    != std::string_view::npos)
                {
                    ++m_Position;
                    _Tokens.push_back({
                        ETokenKind::Symbol,
                        std::string(1, _Character),
                        _Start,
                    });
                    continue;
                }

                throw std::invalid_argument(
                    "Unexpected language character at offset "
                    + std::to_string(_Start));
            }
        }

    private:
        void SkipTrivia()
        {
            while (m_Position < m_Text.size())
            {
                if (std::isspace(static_cast<unsigned char>(
                    m_Text[m_Position])))
                {
                    ++m_Position;
                    continue;
                }
                if (m_Text.substr(m_Position, 2) == "//"
                    || m_Text.substr(m_Position, 2) == "--")
                {
                    m_Position += 2;
                    while (m_Position < m_Text.size()
                        && m_Text[m_Position] != '\n')
                    {
                        ++m_Position;
                    }
                    continue;
                }
                if (m_Text.substr(m_Position, 2) == "/*")
                {
                    const auto _End = m_Text.find("*/", m_Position + 2);
                    if (_End == std::string_view::npos)
                    {
                        throw std::invalid_argument(
                            "Unterminated block comment at offset "
                            + std::to_string(m_Position));
                    }
                    m_Position = _End + 2;
                    continue;
                }
                break;
            }
        }

        SToken ReadString()
        {
            const auto _Start = m_Position;
            const char _Quote = m_Text[m_Position++];
            std::string _Value;
            while (m_Position < m_Text.size())
            {
                const char _Character = m_Text[m_Position++];
                if (_Character == _Quote)
                {
                    return {
                        ETokenKind::String,
                        std::move(_Value),
                        _Start,
                    };
                }
                if (_Character != '\\')
                {
                    _Value.push_back(_Character);
                    continue;
                }
                if (m_Position >= m_Text.size())
                {
                    break;
                }
                const char _Escaped = m_Text[m_Position++];
                switch (_Escaped)
                {
                case 'n':
                    _Value.push_back('\n');
                    break;
                case 'r':
                    _Value.push_back('\r');
                    break;
                case 't':
                    _Value.push_back('\t');
                    break;
                case '\\':
                case '\'':
                case '"':
                    _Value.push_back(_Escaped);
                    break;
                default:
                    _Value.push_back(_Escaped);
                    break;
                }
            }
            throw std::invalid_argument(
                "Unterminated string at offset "
                + std::to_string(_Start));
        }

        SToken ReadNumber()
        {
            const auto _Start = m_Position;
            if (m_Text[m_Position] == '-')
            {
                ++m_Position;
            }
            while (m_Position < m_Text.size()
                && std::isdigit(static_cast<unsigned char>(
                    m_Text[m_Position])))
            {
                ++m_Position;
            }
            if (m_Position < m_Text.size()
                && m_Text[m_Position] == '.')
            {
                ++m_Position;
                while (m_Position < m_Text.size()
                    && std::isdigit(static_cast<unsigned char>(
                        m_Text[m_Position])))
                {
                    ++m_Position;
                }
            }
            if (m_Position < m_Text.size()
                && (m_Text[m_Position] == 'e'
                    || m_Text[m_Position] == 'E'))
            {
                ++m_Position;
                if (m_Position < m_Text.size()
                    && (m_Text[m_Position] == '+'
                        || m_Text[m_Position] == '-'))
                {
                    ++m_Position;
                }
                while (m_Position < m_Text.size()
                    && std::isdigit(static_cast<unsigned char>(
                        m_Text[m_Position])))
                {
                    ++m_Position;
                }
            }
            return {
                ETokenKind::Number,
                std::string(m_Text.substr(
                    _Start,
                    m_Position - _Start)),
                _Start,
            };
        }

    private:
        std::string_view m_Text;
        size_t m_Position = 0;
    };

    class CParser final
    {
    public:
        explicit CParser(IN std::string_view Text_)
            : m_Tokens(CLexer(Text_).Tokenize())
        {
        }

        SEntityWhere ParseLambdaWhere()
        {
            const auto _Variable = ParseLambdaHeader();
            auto _Root = ParseLambdaOr(_Variable);
            MatchSymbol(";");
            ExpectEnd();
            return CEntityWhereBuilder::Build(std::move(_Root));
        }

        SEntityUpdate ParseLambdaUpdate()
        {
            const auto _Variable = ParseLambdaHeader();
            ExpectSymbol("{");
            std::vector<SComponentUpdate> _Components;
            while (!MatchSymbol("}"))
            {
                ExpectIdentifier(_Variable);
                ExpectSymbol(".");
                const auto _Method = Upper(ExpectIdentifier());
                ExpectSymbol("(");
                const auto _ComponentClass = ParseName();
                ExpectSymbol(")");

                SComponentUpdate _Component;
                if (_Method == "MODIFY")
                {
                    _Component =
                        CEntityUpdateBuilder::ModifyComponent(_ComponentClass);
                }
                else if (_Method == "ADD")
                {
                    _Component =
                        CEntityUpdateBuilder::AddComponent(_ComponentClass);
                }
                else if (_Method == "REMOVE")
                {
                    _Component =
                        CEntityUpdateBuilder::RemoveComponent(_ComponentClass);
                }
                else
                {
                    Fail("Expected Modify, Add or Remove");
                }

                while (MatchSymbol("."))
                {
                    const auto _ChainMethod = Upper(ExpectIdentifier());
                    ExpectSymbol("(");
                    if (_ChainMethod == "SET")
                    {
                        if (_Component.Type == EComponentUpdateType::Remove)
                        {
                            Fail("Removed component cannot define properties");
                        }
                        const auto _PropertyName = ParseName();
                        ExpectSymbol(",");
                        auto _Operand = ParseLambdaOperand(_Variable);
                        ExpectSymbol(")");
                        _Component.Properties.insert_or_assign(
                            _PropertyName,
                            std::move(_Operand));
                    }
                    else if (_ChainMethod == "ENABLED")
                    {
                        if (_Component.Type == EComponentUpdateType::Remove)
                        {
                            Fail(
                                "Removed component cannot define enabled state");
                        }
                        _Component.Enabled = ParseBoolean();
                        ExpectSymbol(")");
                    }
                    else
                    {
                        Fail("Expected Set or Enabled");
                    }
                }
                ExpectSymbol(";");
                _Components.push_back(std::move(_Component));
            }
            MatchSymbol(";");
            ExpectEnd();
            return CEntityUpdateBuilder::Build(std::move(_Components));
        }

        SEntityWhere ParseSqlWhere()
        {
            MatchKeyword("WHERE");
            auto _Root = ParseSqlOr();
            MatchSymbol(";");
            ExpectEnd();
            return CEntityWhereBuilder::Build(std::move(_Root));
        }

        SEntityStatement ParseSqlStatement()
        {
            SEntityStatement _Statement;
            if (MatchKeyword("SELECT") || MatchKeyword("QUERY"))
            {
                _Statement.Type = EEntityStatementType::Query;
                ExpectKeyword("ENTITY");
                _Statement.Where = MatchKeyword("WHERE")
                    ? CEntityWhereBuilder::Build(ParseSqlOr())
                    : CEntityWhereBuilder::MatchAll();
            }
            else if (MatchKeyword("UPDATE"))
            {
                _Statement.Type = EEntityStatementType::Update;
                ExpectKeyword("ENTITY");
                std::vector<SComponentUpdate> _Components;
                while (!IsKeyword("WHERE"))
                {
                    if (IsEnd())
                    {
                        Fail("UPDATE ENTITY requires an explicit WHERE");
                    }
                    _Components.push_back(ParseSqlComponentUpdate());
                }
                ExpectKeyword("WHERE");
                _Statement.Where =
                    CEntityWhereBuilder::Build(ParseSqlOr());
                _Statement.Update =
                    CEntityUpdateBuilder::Build(std::move(_Components));
            }
            else if (MatchKeyword("DELETE"))
            {
                _Statement.Type = EEntityStatementType::Delete;
                ExpectKeyword("ENTITY");
                ExpectKeyword("WHERE");
                _Statement.Where =
                    CEntityWhereBuilder::Build(ParseSqlOr());
            }
            else
            {
                Fail("Expected SELECT, QUERY, UPDATE or DELETE");
            }

            MatchSymbol(";");
            ExpectEnd();
            return _Statement;
        }

    private:
        std::string ParseLambdaHeader()
        {
            std::string _Variable;
            if (MatchSymbol("("))
            {
                _Variable = ExpectIdentifier();
                ExpectSymbol(")");
            }
            else
            {
                _Variable = ExpectIdentifier();
            }
            ExpectSymbol("=>");
            return _Variable;
        }

        SEntityWhereNode ParseLambdaOr(IN const std::string& Variable_)
        {
            auto _Left = ParseLambdaAnd(Variable_);
            while (MatchSymbol("||"))
            {
                auto _Right = ParseLambdaAnd(Variable_);
                _Left = CEntityWhereBuilder::Any({
                    std::move(_Left),
                    std::move(_Right),
                });
            }
            return _Left;
        }

        SEntityWhereNode ParseLambdaAnd(IN const std::string& Variable_)
        {
            auto _Left = ParseLambdaUnary(Variable_);
            while (MatchSymbol("&&"))
            {
                auto _Right = ParseLambdaUnary(Variable_);
                _Left = CEntityWhereBuilder::All({
                    std::move(_Left),
                    std::move(_Right),
                });
            }
            return _Left;
        }

        SEntityWhereNode ParseLambdaUnary(IN const std::string& Variable_)
        {
            if (MatchSymbol("!"))
            {
                return CEntityWhereBuilder::Not(
                    ParseLambdaUnary(Variable_));
            }
            if (MatchSymbol("("))
            {
                auto _Result = ParseLambdaOr(Variable_);
                ExpectSymbol(")");
                return _Result;
            }
            if (MatchKeyword("TRUE"))
            {
                return CEntityWhereBuilder::Constant(true);
            }
            if (MatchKeyword("FALSE"))
            {
                return CEntityWhereBuilder::Constant(false);
            }

            ExpectIdentifier(Variable_);
            ExpectSymbol(".");
            const auto _Method = Upper(ExpectIdentifier());
            ExpectSymbol("(");

            if (_Method == "HAS" || _Method == "ENABLED")
            {
                const auto _ComponentClass = ParseName();
                ExpectSymbol(")");
                return _Method == "HAS"
                    ? CEntityWhereBuilder::HasComponent(_ComponentClass)
                    : CEntityWhereBuilder::ComponentEnabled(
                        _ComponentClass);
            }
            if (_Method == "CONSTANT")
            {
                const bool _Value = ParseBoolean();
                ExpectSymbol(")");
                return CEntityWhereBuilder::Constant(_Value);
            }
            if (_Method == "FIELD")
            {
                const auto _ComponentClass = ParseName();
                ExpectSymbol(",");
                const auto _PropertyPath = ParseName();
                ExpectSymbol(")");
                return ParseLambdaFieldComparison(
                    Variable_,
                    _ComponentClass,
                    _PropertyPath);
            }
            if (_Method == "REF" || _Method == "REFERENCE")
            {
                const auto _ComponentClass = ParseName();
                ExpectSymbol(",");
                const auto _PropertyPath = ParseName();
                ExpectSymbol(")");
                ExpectSymbol(".");
                const auto _MatchMethod = Upper(ExpectIdentifier());
                if (_MatchMethod != "ANY" && _MatchMethod != "ALL")
                {
                    Fail("Reference requires Any or All");
                }
                ExpectSymbol("(");
                const auto _TargetVariable = ParseLambdaHeader();
                auto _Target = ParseLambdaOr(_TargetVariable);
                ExpectSymbol(")");
                return CEntityWhereBuilder::Reference(
                    _ComponentClass,
                    _PropertyPath,
                    std::move(_Target),
                    _MatchMethod == "ANY"
                        ? EEntityReferenceMatch::Any
                        : EEntityReferenceMatch::All);
            }

            Fail("Unknown Entity lambda method");
        }

        SEntityWhereNode ParseLambdaFieldComparison(
            IN const std::string& Variable_,
            IN const std::string& ComponentClass_,
            IN const std::string& PropertyPath_)
        {
            EEntityWhereComparison _Comparison;
            if (MatchSymbol("=="))
            {
                _Comparison = EEntityWhereComparison::Equal;
            }
            else if (MatchSymbol("!="))
            {
                _Comparison = EEntityWhereComparison::NotEqual;
            }
            else if (MatchSymbol("<="))
            {
                _Comparison = EEntityWhereComparison::LessOrEqual;
            }
            else if (MatchSymbol(">="))
            {
                _Comparison = EEntityWhereComparison::GreaterOrEqual;
            }
            else if (MatchSymbol("<"))
            {
                _Comparison = EEntityWhereComparison::Less;
            }
            else if (MatchSymbol(">"))
            {
                _Comparison = EEntityWhereComparison::Greater;
            }
            else if (MatchSymbol("."))
            {
                const auto _Method = Upper(ExpectIdentifier());
                if (_Method == "CONTAINS")
                {
                    _Comparison = EEntityWhereComparison::Contains;
                }
                else if (_Method == "STARTSWITH")
                {
                    _Comparison = EEntityWhereComparison::StartsWith;
                }
                else if (_Method == "ENDSWITH")
                {
                    _Comparison = EEntityWhereComparison::EndsWith;
                }
                else if (_Method == "IN")
                {
                    _Comparison = EEntityWhereComparison::In;
                }
                else
                {
                    Fail("Unknown field comparison method");
                }
                ExpectSymbol("(");
                auto _Operand = ParseLambdaOperand(Variable_);
                ExpectSymbol(")");
                return CEntityWhereBuilder::Compare(
                    ComponentClass_,
                    PropertyPath_,
                    _Comparison,
                    std::move(_Operand));
            }
            else
            {
                Fail("Field expression requires a comparison");
            }

            return CEntityWhereBuilder::Compare(
                ComponentClass_,
                PropertyPath_,
                _Comparison,
                ParseLambdaOperand(Variable_));
        }

        SEntityValueOperand ParseLambdaOperand(
            IN const std::string& Variable_)
        {
            if (IsIdentifier(Variable_)
                && IsSymbol(".", 1)
                && IsKeyword("PARAMETER", 2))
            {
                ++m_Index;
                ExpectSymbol(".");
                ExpectKeyword("PARAMETER");
                ExpectSymbol("(");
                const auto _Name = ParseName();
                ExpectSymbol(")");
                return CEntityWhereBuilder::Parameter(_Name);
            }
            if (MatchKeyword("PARAMETER"))
            {
                ExpectSymbol("(");
                const auto _Name = ParseName();
                ExpectSymbol(")");
                return CEntityWhereBuilder::Parameter(_Name);
            }
            return ParseLiteralOperand(true);
        }

        SEntityWhereNode ParseSqlOr()
        {
            auto _Left = ParseSqlAnd();
            while (MatchKeyword("OR") || MatchSymbol("||"))
            {
                auto _Right = ParseSqlAnd();
                _Left = CEntityWhereBuilder::Any({
                    std::move(_Left),
                    std::move(_Right),
                });
            }
            return _Left;
        }

        SEntityWhereNode ParseSqlAnd()
        {
            auto _Left = ParseSqlUnary();
            while (MatchKeyword("AND") || MatchSymbol("&&"))
            {
                auto _Right = ParseSqlUnary();
                _Left = CEntityWhereBuilder::All({
                    std::move(_Left),
                    std::move(_Right),
                });
            }
            return _Left;
        }

        SEntityWhereNode ParseSqlUnary()
        {
            if (MatchKeyword("NOT") || MatchSymbol("!"))
            {
                return CEntityWhereBuilder::Not(ParseSqlUnary());
            }
            if (MatchSymbol("("))
            {
                auto _Result = ParseSqlOr();
                ExpectSymbol(")");
                return _Result;
            }
            if (MatchKeyword("TRUE"))
            {
                return CEntityWhereBuilder::Constant(true);
            }
            if (MatchKeyword("FALSE"))
            {
                return CEntityWhereBuilder::Constant(false);
            }
            if (MatchKeyword("HAS"))
            {
                return CEntityWhereBuilder::HasComponent(
                    ParseSqlComponentArgument());
            }
            if (MatchKeyword("ENABLED"))
            {
                return CEntityWhereBuilder::ComponentEnabled(
                    ParseSqlComponentArgument());
            }
            if (MatchKeyword("REF") || MatchKeyword("REFERENCE"))
            {
                const auto [_ComponentClass, _PropertyPath] =
                    ParseSqlFieldPath();
                EEntityReferenceMatch _Match;
                if (MatchKeyword("ANY"))
                {
                    _Match = EEntityReferenceMatch::Any;
                }
                else if (MatchKeyword("ALL"))
                {
                    _Match = EEntityReferenceMatch::All;
                }
                else
                {
                    Fail("Reference requires ANY or ALL");
                }
                ExpectSymbol("(");
                auto _Target = ParseSqlOr();
                ExpectSymbol(")");
                return CEntityWhereBuilder::Reference(
                    _ComponentClass,
                    _PropertyPath,
                    std::move(_Target),
                    _Match);
            }

            const auto [_ComponentClass, _PropertyPath] =
                ParseSqlFieldPath();
            EEntityWhereComparison _Comparison;
            if (MatchSymbol("=") || MatchSymbol("=="))
            {
                _Comparison = EEntityWhereComparison::Equal;
            }
            else if (MatchSymbol("!=") || MatchSymbol("<>"))
            {
                _Comparison = EEntityWhereComparison::NotEqual;
            }
            else if (MatchSymbol("<="))
            {
                _Comparison = EEntityWhereComparison::LessOrEqual;
            }
            else if (MatchSymbol(">="))
            {
                _Comparison = EEntityWhereComparison::GreaterOrEqual;
            }
            else if (MatchSymbol("<"))
            {
                _Comparison = EEntityWhereComparison::Less;
            }
            else if (MatchSymbol(">"))
            {
                _Comparison = EEntityWhereComparison::Greater;
            }
            else if (MatchKeyword("CONTAINS"))
            {
                _Comparison = EEntityWhereComparison::Contains;
            }
            else if (MatchKeyword("STARTS_WITH")
                || MatchKeyword("STARTSWITH"))
            {
                _Comparison = EEntityWhereComparison::StartsWith;
            }
            else if (MatchKeyword("ENDS_WITH")
                || MatchKeyword("ENDSWITH"))
            {
                _Comparison = EEntityWhereComparison::EndsWith;
            }
            else if (MatchKeyword("IN"))
            {
                _Comparison = EEntityWhereComparison::In;
            }
            else
            {
                Fail("EntitySQL field requires a comparison");
            }

            return CEntityWhereBuilder::Compare(
                _ComponentClass,
                _PropertyPath,
                _Comparison,
                ParseSqlOperand(_Comparison == EEntityWhereComparison::In));
        }

        SComponentUpdate ParseSqlComponentUpdate()
        {
            EComponentUpdateType _Type;
            if (MatchKeyword("MODIFY"))
            {
                _Type = EComponentUpdateType::Modify;
            }
            else if (MatchKeyword("ADD"))
            {
                _Type = EComponentUpdateType::Add;
            }
            else if (MatchKeyword("REMOVE"))
            {
                _Type = EComponentUpdateType::Remove;
            }
            else
            {
                Fail("Expected MODIFY, ADD or REMOVE");
            }

            const auto _ComponentClass = ParseName();
            if (_Type == EComponentUpdateType::Remove)
            {
                return CEntityUpdateBuilder::RemoveComponent(
                    _ComponentClass);
            }

            if (_Type == EComponentUpdateType::Modify)
            {
                ExpectKeyword("SET");
            }
            else if (!MatchKeyword("WITH"))
            {
                return CEntityUpdateBuilder::AddComponent(_ComponentClass);
            }

            std::map<std::string, SEntityValueOperand> _Properties;
            std::optional<bool> _Enabled =
                _Type == EComponentUpdateType::Add
                    ? std::optional<bool>(true)
                    : std::nullopt;
            while (true)
            {
                const auto _PropertyName = ParseName();
                ExpectSymbol("=");
                if (Upper(_PropertyName) == "ENABLED")
                {
                    _Enabled = ParseBoolean();
                }
                else
                {
                    _Properties.insert_or_assign(
                        _PropertyName,
                        ParseSqlOperand(false));
                }
                if (!MatchSymbol(","))
                {
                    break;
                }
            }

            return _Type == EComponentUpdateType::Modify
                ? CEntityUpdateBuilder::ModifyComponent(
                    _ComponentClass,
                    std::move(_Properties),
                    _Enabled)
                : CEntityUpdateBuilder::AddComponent(
                    _ComponentClass,
                    std::move(_Properties),
                    _Enabled.value_or(true));
        }

        SEntityValueOperand ParseSqlOperand(IN const bool bAllowList_)
        {
            if (MatchSymbol(":"))
            {
                return CEntityWhereBuilder::Parameter(
                    ExpectIdentifier());
            }
            if (MatchKeyword("PARAMETER"))
            {
                ExpectSymbol("(");
                const auto _Name = ParseName();
                ExpectSymbol(")");
                return CEntityWhereBuilder::Parameter(_Name);
            }
            if (bAllowList_ && MatchSymbol("("))
            {
                VariantArray _Values;
                if (!MatchSymbol(")"))
                {
                    while (true)
                    {
                        auto _Operand = ParseLiteralOperand(false);
                        if (_Operand.Type
                            != EEntityValueOperandType::Literal)
                        {
                            Fail(
                                "IN literal list cannot contain parameters");
                        }
                        _Values.push_back(std::move(_Operand.Literal));
                        if (!MatchSymbol(","))
                        {
                            break;
                        }
                    }
                    ExpectSymbol(")");
                }
                return CEntityWhereBuilder::Literal(
                    PropertyValue(std::move(_Values)));
            }
            return ParseLiteralOperand(false);
        }

        SEntityValueOperand ParseLiteralOperand(
            IN const bool bAllowArray_)
        {
            if (Peek().Kind == ETokenKind::String)
            {
                return CEntityWhereBuilder::Literal(
                    PropertyValue(Consume().Text));
            }
            if (Peek().Kind == ETokenKind::Number)
            {
                const auto _Text = Consume().Text;
                if (_Text.find_first_of(".eE") != std::string::npos)
                {
                    return CEntityWhereBuilder::Literal(
                        PropertyValue(std::stod(_Text)));
                }
                const auto _Value = std::stoll(_Text);
                if (_Value >= (std::numeric_limits<int>::min)()
                    && _Value <= (std::numeric_limits<int>::max)())
                {
                    return CEntityWhereBuilder::Literal(
                        PropertyValue(static_cast<int>(_Value)));
                }
                return CEntityWhereBuilder::Literal(
                    PropertyValue(_Value));
            }
            if (MatchKeyword("TRUE"))
            {
                return CEntityWhereBuilder::Literal(PropertyValue(true));
            }
            if (MatchKeyword("FALSE"))
            {
                return CEntityWhereBuilder::Literal(PropertyValue(false));
            }
            if (MatchKeyword("NULL") || MatchKeyword("NIL"))
            {
                return CEntityWhereBuilder::Literal(PropertyValue::Nil);
            }
            if (bAllowArray_ && MatchSymbol("["))
            {
                VariantArray _Values;
                if (!MatchSymbol("]"))
                {
                    while (true)
                    {
                        auto _Operand = ParseLiteralOperand(false);
                        _Values.push_back(std::move(_Operand.Literal));
                        if (!MatchSymbol(","))
                        {
                            break;
                        }
                    }
                    ExpectSymbol("]");
                }
                return CEntityWhereBuilder::Literal(
                    PropertyValue(std::move(_Values)));
            }
            Fail("Expected literal or parameter");
        }

        std::string ParseSqlComponentArgument()
        {
            if (MatchSymbol("("))
            {
                auto _Name = ParseName();
                ExpectSymbol(")");
                return _Name;
            }
            return ParseName();
        }

        std::pair<std::string, std::string> ParseSqlFieldPath()
        {
            const auto _ComponentClass = ParseName();
            ExpectSymbol(".");
            std::string _PropertyPath = ParseName();
            while (true)
            {
                if (MatchSymbol("."))
                {
                    _PropertyPath += ".";
                    _PropertyPath += ParseName();
                    continue;
                }
                if (MatchSymbol("["))
                {
                    _PropertyPath += "[";
                    if (Peek().Kind != ETokenKind::Number
                        && Peek().Kind != ETokenKind::String
                        && Peek().Kind != ETokenKind::Identifier)
                    {
                        Fail("Invalid property path index");
                    }
                    _PropertyPath += Consume().Text;
                    ExpectSymbol("]");
                    _PropertyPath += "]";
                    continue;
                }
                break;
            }
            return { _ComponentClass, _PropertyPath };
        }

        bool ParseBoolean()
        {
            if (MatchKeyword("TRUE"))
            {
                return true;
            }
            if (MatchKeyword("FALSE"))
            {
                return false;
            }
            Fail("Expected TRUE or FALSE");
        }

        std::string ParseName()
        {
            if (Peek().Kind == ETokenKind::Identifier
                || Peek().Kind == ETokenKind::String)
            {
                return Consume().Text;
            }
            Fail("Expected a name");
        }

        bool IsEnd() const
        {
            return Peek().Kind == ETokenKind::End;
        }

        void ExpectEnd() const
        {
            if (!IsEnd())
            {
                Fail("Unexpected trailing input");
            }
        }

        const SToken& Peek(IN const size_t nOffset_ = 0) const
        {
            const auto _Index = (std::min)(
                m_Index + nOffset_,
                m_Tokens.size() - 1);
            return m_Tokens[_Index];
        }

        SToken Consume()
        {
            const auto _Token = Peek();
            if (m_Index + 1 < m_Tokens.size())
            {
                ++m_Index;
            }
            return _Token;
        }

        bool IsKeyword(
            IN std::string_view Keyword_,
            IN const size_t nOffset_ = 0) const
        {
            const auto& _Token = Peek(nOffset_);
            return _Token.Kind == ETokenKind::Identifier
                && Upper(_Token.Text) == Upper(Keyword_);
        }

        bool MatchKeyword(IN std::string_view Keyword_)
        {
            if (!IsKeyword(Keyword_))
            {
                return false;
            }
            Consume();
            return true;
        }

        void ExpectKeyword(IN std::string_view Keyword_)
        {
            if (!MatchKeyword(Keyword_))
            {
                Fail("Expected keyword " + std::string(Keyword_));
            }
        }

        bool IsIdentifier(
            IN std::string_view Identifier_,
            IN const size_t nOffset_ = 0) const
        {
            const auto& _Token = Peek(nOffset_);
            return _Token.Kind == ETokenKind::Identifier
                && _Token.Text == Identifier_;
        }

        std::string ExpectIdentifier()
        {
            if (Peek().Kind != ETokenKind::Identifier)
            {
                Fail("Expected identifier");
            }
            return Consume().Text;
        }

        void ExpectIdentifier(IN std::string_view Identifier_)
        {
            if (!IsIdentifier(Identifier_))
            {
                Fail("Expected identifier " + std::string(Identifier_));
            }
            Consume();
        }

        bool IsSymbol(
            IN std::string_view Symbol_,
            IN const size_t nOffset_ = 0) const
        {
            const auto& _Token = Peek(nOffset_);
            return _Token.Kind == ETokenKind::Symbol
                && _Token.Text == Symbol_;
        }

        bool MatchSymbol(IN std::string_view Symbol_)
        {
            if (!IsSymbol(Symbol_))
            {
                return false;
            }
            Consume();
            return true;
        }

        void ExpectSymbol(IN std::string_view Symbol_)
        {
            if (!MatchSymbol(Symbol_))
            {
                Fail("Expected symbol " + std::string(Symbol_));
            }
        }

        [[noreturn]] void Fail(IN const std::string& Message_) const
        {
            throw std::invalid_argument(
                Message_
                + " at offset "
                + std::to_string(Peek().Position));
        }

    private:
        std::vector<SToken> m_Tokens;
        size_t m_Index = 0;
    };
}

iCAX::Database::SEntityWhere
iCAX::DatabaseLanguage::CEntityLambda::ParseWhere(
    IN std::string_view Text_)
{
    return CParser(Text_).ParseLambdaWhere();
}

iCAX::Database::SEntityUpdate
iCAX::DatabaseLanguage::CEntityLambda::ParseUpdate(
    IN std::string_view Text_)
{
    return CParser(Text_).ParseLambdaUpdate();
}

iCAX::Database::SEntityWhere
iCAX::DatabaseLanguage::CEntitySql::ParseWhere(
    IN std::string_view Text_)
{
    return CParser(Text_).ParseSqlWhere();
}

iCAX::DatabaseLanguage::SEntityStatement
iCAX::DatabaseLanguage::CEntitySql::Parse(
    IN std::string_view Text_)
{
    return CParser(Text_).ParseSqlStatement();
}

iCAX::DatabaseLanguage::SEntityExecutionResult
iCAX::DatabaseLanguage::CEntitySql::Execute(
    IN iCAX::Database::IRepository& Repository_,
    IN std::string_view Text_,
    IN const iCAX::Data::ObjectMap& Parameters_)
{
    auto _Statement = Parse(Text_);
    SEntityExecutionResult _Result;
    _Result.Type = _Statement.Type;
    switch (_Statement.Type)
    {
    case EEntityStatementType::Query:
        _Result.EntityIDs = Repository_.Query(
            _Statement.Where,
            Parameters_);
        break;
    case EEntityStatementType::Update:
        _Result.Mutation = Repository_.Update(
            _Statement.Where,
            _Statement.Update,
            Parameters_);
        break;
    case EEntityStatementType::Delete:
        _Result.Mutation = Repository_.Delete(
            _Statement.Where,
            Parameters_);
        break;
    }
    return _Result;
}
