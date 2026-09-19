#include "pch.h"

#include "BatchExcel.h"
#include "PartListXlsxExporter.h"

#include "TemplateRuntime/StandardJsonCodec.h"
#include "TemplateRuntime/TemplateContracts.h"

#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <unordered_map>

#include <zlib.h>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    using iCAX::TemplateRuntime::EParameterValueType;
    using iCAX::TemplateRuntime::STemplateDescriptor;
    using iCAX::TubeDesigner::SBatchExcelChoice;
    using iCAX::TubeDesigner::SBatchExcelColumn;

    constexpr std::uint64_t kMaximumWorkbookBytes = 32ull * 1024ull * 1024ull;
    constexpr std::uint64_t kMaximumWorksheetBytes = 16ull * 1024ull * 1024ull;

    std::string ReadText(const std::filesystem::path& Path_)
    {
        if (!std::filesystem::is_regular_file(Path_))
            throw std::invalid_argument("Excel 导入文件不存在: " + Path_.string());
        const auto _Size = std::filesystem::file_size(Path_);
        if (_Size > kMaximumWorkbookBytes)
            throw std::invalid_argument("Excel 文件超过 32 MiB 限制");
        std::ifstream _Input(Path_, std::ios::binary);
        if (!_Input) throw std::runtime_error("无法读取 Excel 导入文件");
        std::string _Text(static_cast<std::size_t>(_Size), '\0');
        _Input.read(_Text.data(), static_cast<std::streamsize>(_Text.size()));
        if (!_Input && !_Input.eof()) throw std::runtime_error("Excel 导入文件读取不完整");
        return _Text;
    }

    std::uint16_t UInt16(const std::string& Data_, std::size_t Offset_)
    {
        if (Offset_ + 2 > Data_.size()) throw std::invalid_argument("Excel ZIP 文件损坏");
        return static_cast<std::uint16_t>(static_cast<unsigned char>(Data_[Offset_]))
            | (static_cast<std::uint16_t>(static_cast<unsigned char>(Data_[Offset_ + 1])) << 8);
    }

    std::uint32_t UInt32(const std::string& Data_, std::size_t Offset_)
    {
        if (Offset_ + 4 > Data_.size()) throw std::invalid_argument("Excel ZIP 文件损坏");
        return static_cast<std::uint32_t>(static_cast<unsigned char>(Data_[Offset_]))
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(Data_[Offset_ + 1])) << 8)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(Data_[Offset_ + 2])) << 16)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(Data_[Offset_ + 3])) << 24);
    }

    struct SZipEntry final { std::uint16_t Method = 0; std::uint32_t Compressed = 0; std::uint32_t Uncompressed = 0; std::uint32_t Offset = 0; };

    std::unordered_map<std::string, SZipEntry> ReadZipIndex(const std::string& Archive_)
    {
        const auto _Minimum = std::size_t(22);
        if (Archive_.size() < _Minimum) throw std::invalid_argument("Excel 文件不是有效的 XLSX 压缩包");
        const auto _Start = Archive_.size() > 65557 ? Archive_.size() - 65557 : 0;
        std::size_t _End = std::string::npos;
        for (auto _Index = Archive_.size() - _Minimum + 1; _Index-- > _Start;)
        {
            if (UInt32(Archive_, _Index) == 0x06054B50u) { _End = _Index; break; }
        }
        if (_End == std::string::npos) throw std::invalid_argument("Excel XLSX 缺少 ZIP 目录");
        const auto _Count = UInt16(Archive_, _End + 10);
        auto _Position = static_cast<std::size_t>(UInt32(Archive_, _End + 16));
        std::unordered_map<std::string, SZipEntry> _Result;
        for (std::uint16_t _Index = 0; _Index < _Count; ++_Index)
        {
            if (UInt32(Archive_, _Position) != 0x02014B50u) throw std::invalid_argument("Excel ZIP 目录损坏");
            const auto _Method = UInt16(Archive_, _Position + 10);
            const auto _Compressed = UInt32(Archive_, _Position + 20);
            const auto _Uncompressed = UInt32(Archive_, _Position + 24);
            const auto _NameLength = UInt16(Archive_, _Position + 28);
            const auto _ExtraLength = UInt16(Archive_, _Position + 30);
            const auto _CommentLength = UInt16(Archive_, _Position + 32);
            const auto _Offset = UInt32(Archive_, _Position + 42);
            const auto _NameStart = _Position + 46;
            if (_NameStart + _NameLength > Archive_.size()) throw std::invalid_argument("Excel ZIP 名称损坏");
            _Result.emplace(Archive_.substr(_NameStart, _NameLength), SZipEntry{ _Method, _Compressed, _Uncompressed, _Offset });
            _Position = _NameStart + _NameLength + _ExtraLength + _CommentLength;
        }
        return _Result;
    }

    std::string InflateZipEntry(const std::string& Archive_, const SZipEntry& Entry_)
    {
        if (Entry_.Uncompressed > kMaximumWorksheetBytes) throw std::invalid_argument("Excel 工作表超过 16 MiB 限制");
        const auto _Offset = static_cast<std::size_t>(Entry_.Offset);
        if (UInt32(Archive_, _Offset) != 0x04034B50u) throw std::invalid_argument("Excel ZIP 条目损坏");
        const auto _NameLength = UInt16(Archive_, _Offset + 26);
        const auto _ExtraLength = UInt16(Archive_, _Offset + 28);
        const auto _DataOffset = _Offset + 30 + _NameLength + _ExtraLength;
        if (_DataOffset + Entry_.Compressed > Archive_.size()) throw std::invalid_argument("Excel ZIP 内容损坏");
        if (Entry_.Method == 0) return Archive_.substr(_DataOffset, Entry_.Compressed);
        if (Entry_.Method != 8) throw std::invalid_argument("Excel 工作簿使用了不支持的压缩方式");
        std::string _Result(Entry_.Uncompressed, '\0');
        z_stream _Stream{};
        _Stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(Archive_.data() + _DataOffset));
        _Stream.avail_in = Entry_.Compressed;
        _Stream.next_out = reinterpret_cast<Bytef*>(_Result.data());
        _Stream.avail_out = Entry_.Uncompressed;
        if (inflateInit2(&_Stream, -MAX_WBITS) != Z_OK) throw std::runtime_error("Excel 解压初始化失败");
        const auto _ResultCode = inflate(&_Stream, Z_FINISH);
        inflateEnd(&_Stream);
        if (_ResultCode != Z_STREAM_END || _Stream.total_out != Entry_.Uncompressed)
            throw std::invalid_argument("Excel 工作表解压失败");
        return _Result;
    }

    std::string DecodeXml(std::string Value_)
    {
        const std::array<std::pair<std::string, std::string>, 5> _Entities{{
            { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&apos;", "'" }
        }};
        for (const auto& [_From, _To] : _Entities)
        {
            std::size_t _Position = 0;
            while ((_Position = Value_.find(_From, _Position)) != std::string::npos)
            {
                Value_.replace(_Position, _From.size(), _To);
                _Position += _To.size();
            }
        }
        return Value_;
    }

    std::size_t ColumnIndex(const std::string& Reference_)
    {
        std::size_t _Value = 0;
        for (const auto _Character : Reference_)
        {
            if (std::isalpha(static_cast<unsigned char>(_Character)))
                _Value = _Value * 26 + static_cast<std::size_t>(std::toupper(static_cast<unsigned char>(_Character)) - 'A' + 1);
            else break;
        }
        return _Value ? _Value - 1 : std::numeric_limits<std::size_t>::max();
    }

    std::vector<std::string> SharedStrings(const std::unordered_map<std::string, SZipEntry>& Index_, const std::string& Archive_)
    {
        const auto _Found = Index_.find("xl/sharedStrings.xml");
        if (_Found == Index_.end()) return {};
        const auto _Xml = InflateZipEntry(Archive_, _Found->second);
        const std::regex _Pattern("<si>([\\s\\S]*?)</si>");
        const std::regex _Text("<t(?: [^>]*)?>([\\s\\S]*?)</t>");
        std::vector<std::string> _Result;
        for (std::sregex_iterator _It(_Xml.begin(), _Xml.end(), _Pattern), _End; _It != _End; ++_It)
        {
            std::string _Value;
            const auto _Item = (*_It)[1].str();
            for (std::sregex_iterator _TextIt(_Item.begin(), _Item.end(), _Text), _TextEnd; _TextIt != _TextEnd; ++_TextIt)
                _Value += DecodeXml((*_TextIt)[1].str());
            _Result.push_back(std::move(_Value));
        }
        return _Result;
    }

    std::vector<std::vector<std::string>> ReadWorksheet(
        const std::filesystem::path& Path_, const std::string& WorksheetEntry_ = "xl/worksheets/sheet1.xml")
    {
        const auto _Archive = ReadText(Path_);
        const auto _Index = ReadZipIndex(_Archive);
        const auto _Sheet = _Index.find(WorksheetEntry_);
        if (_Sheet == _Index.end()) throw std::invalid_argument("Excel 模板缺少工作表：" + WorksheetEntry_);
        const auto _Shared = SharedStrings(_Index, _Archive);
        const auto _Xml = InflateZipEntry(_Archive, _Sheet->second);
        const std::regex _RowPattern("<row(?: [^>]*)?>([\\s\\S]*?)</row>");
        const std::regex _CellPattern("<c r=\"([^\"]+)\"([^>]*)>([\\s\\S]*?)</c>");
        const std::regex _TypePattern(" t=\"([^\"]+)\"");
        const std::regex _ValuePattern("<v>([\\s\\S]*?)</v>");
        const std::regex _InlinePattern("<t(?: [^>]*)?>([\\s\\S]*?)</t>");
        std::vector<std::vector<std::string>> _Rows;
        for (std::sregex_iterator _RowIt(_Xml.begin(), _Xml.end(), _RowPattern), _RowEnd; _RowIt != _RowEnd; ++_RowIt)
        {
            const auto _RowXml = (*_RowIt)[1].str();
            std::vector<std::string> _Row;
            for (std::sregex_iterator _CellIt(_RowXml.begin(), _RowXml.end(), _CellPattern), _CellEnd; _CellIt != _CellEnd; ++_CellIt)
            {
                const auto _Column = ColumnIndex((*_CellIt)[1].str());
                if (_Column == std::numeric_limits<std::size_t>::max()) continue;
                if (_Row.size() <= _Column) _Row.resize(_Column + 1);
                std::smatch _TypeMatch;
                const auto _Attributes = (*_CellIt)[2].str();
                const auto _Type = std::regex_search(_Attributes, _TypeMatch, _TypePattern) ? _TypeMatch[1].str() : std::string();
                const auto _Body = (*_CellIt)[3].str();
                std::smatch _Match;
                std::string _Value;
                if (_Type == "inlineStr")
                {
                    for (std::sregex_iterator _TextIt(_Body.begin(), _Body.end(), _InlinePattern), _TextEnd; _TextIt != _TextEnd; ++_TextIt)
                        _Value += DecodeXml((*_TextIt)[1].str());
                }
                else if (std::regex_search(_Body, _Match, _ValuePattern))
                {
                    _Value = DecodeXml(_Match[1].str());
                    if (_Type == "s")
                    {
                        std::size_t _SharedIndex = 0;
                        const auto [_Pointer, _Error] = std::from_chars(_Value.data(), _Value.data() + _Value.size(), _SharedIndex);
                        if (_Error != std::errc{} || _SharedIndex >= _Shared.size()) throw std::invalid_argument("Excel 共享文本索引无效");
                        _Value = _Shared[_SharedIndex];
                    }
                }
                _Row[_Column] = std::move(_Value);
            }
            _Rows.push_back(std::move(_Row));
        }
        return _Rows;
    }

    std::string RequiredText(const ObjectMap& Value_, const char* Key_)
    {
        const auto _It = Value_.find(Key_);
        if (_It == Value_.end() || !_It->second.Is<std::string>() || _It->second.To<std::string>().empty())
            throw std::invalid_argument(std::string("Excel 导入定义缺少 ") + Key_);
        return _It->second.To<std::string>();
    }

    std::string OptionalText(const ObjectMap& Value_, const char* Key_)
    {
        const auto _It = Value_.find(Key_);
        return _It != Value_.end() && _It->second.Is<std::string>() ? _It->second.To<std::string>() : std::string();
    }

    bool OptionalBool(const ObjectMap& Value_, const char* Key_, bool Default_ = false)
    {
        const auto _It = Value_.find(Key_);
        return _It != Value_.end() && _It->second.Is<bool>() ? _It->second.To<bool>() : Default_;
    }

    std::string ChoiceValueType(const Variant& Value_)
    {
        if (Value_.Is<std::string>()) return "string";
        if (Value_.Is<bool>()) return "boolean";
        if (Value_.Is<double>()) return "double";
        if (Value_.Is<unsigned long long>()) return "unsignedInteger";
        if (Value_.Is<long long>()) return "signedInteger";
        throw std::invalid_argument("Excel 枚举选项值必须是文本、数字或布尔值");
    }

    Variant RestoreChoiceValue(const Variant& Value_, const std::string& ValueType_)
    {
        if (ValueType_ == "string" && Value_.Is<std::string>()) return Value_;
        if (ValueType_ == "boolean" && Value_.Is<bool>()) return Value_;
        if (ValueType_ == "double")
        {
            if (Value_.Is<double>()) return Value_;
            if (Value_.Is<unsigned long long>()) return Variant(static_cast<double>(Value_.To<unsigned long long>()));
            if (Value_.Is<long long>()) return Variant(static_cast<double>(Value_.To<long long>()));
        }
        if (ValueType_ == "unsignedInteger")
        {
            if (Value_.Is<unsigned long long>()) return Value_;
            if (Value_.Is<long long>() && Value_.To<long long>() >= 0)
                return Variant(static_cast<unsigned long long>(Value_.To<long long>()));
        }
        if (ValueType_ == "signedInteger")
        {
            if (Value_.Is<long long>()) return Value_;
            if (Value_.Is<unsigned long long>()
                && Value_.To<unsigned long long>() <= static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
            {
                return Variant(static_cast<long long>(Value_.To<unsigned long long>()));
            }
        }
        throw std::invalid_argument("Excel 内置列选项值类型无效");
    }

    std::vector<SBatchExcelChoice> OptionalChoices(const ObjectMap& Value_)
    {
        const auto _It = Value_.find("choices");
        if (_It == Value_.end()) return {};
        if (!_It->second.Is<VariantArray>()) throw std::invalid_argument("Excel 内置列选项格式无效");
        std::vector<SBatchExcelChoice> _Result;
        for (const auto& _Value : _It->second.To<VariantArray>())
        {
            if (!_Value.Is<ObjectMap>()) throw std::invalid_argument("Excel 内置列选项无效");
            const auto _Choice = _Value.To<ObjectMap>();
            const auto _ChoiceValue = _Choice.find("value");
            if (_ChoiceValue == _Choice.end()) throw std::invalid_argument("Excel 内置列选项缺少 value");
            const auto _ValueType = RequiredText(_Choice, "valueType");
            _Result.push_back({ RestoreChoiceValue(_ChoiceValue->second, _ValueType),
                RequiredText(_Choice, "label"), _ValueType });
        }
        return _Result;
    }

    std::string ParameterText(const Variant& Value_)
    {
        if (Value_.Is<std::string>()) return Value_.To<std::string>();
        const auto _Text = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Value_);
        if (_Text.empty() || _Text == "null" || _Text.front() == '{' || _Text.front() == '[' || _Text.front() == '"')
            throw std::invalid_argument("Excel 枚举选项值必须是文本、数字或布尔值");
        return _Text;
    }

    Variant ResolveChoiceValue(
        const SBatchExcelColumn& Column_, const std::string& Value_, const std::uint64_t Row_)
    {
        if (Column_.Choices.empty() || Value_.empty()) return Variant(Value_);
        for (const auto& _Choice : Column_.Choices)
            if (ParameterText(_Choice.Value) == Value_) return _Choice.Value;
        for (const auto& _Choice : Column_.Choices)
            if (_Choice.Label == Value_) return _Choice.Value;
        std::string _Allowed;
        for (const auto& _Choice : Column_.Choices)
        {
            if (!_Allowed.empty()) _Allowed += "、";
            _Allowed += _Choice.Label;
        }
        throw std::invalid_argument("Excel 第 " + std::to_string(Row_) + " 行的“" + Column_.Title
            + "”必须从中文选项中选择：" + _Allowed);
    }

    std::uint64_t ParseQuantity(const std::string& Value_, const std::uint64_t Row_)
    {
        std::uint64_t _Value = 0;
        const auto [_Pointer, _Error] = std::from_chars(Value_.data(), Value_.data() + Value_.size(), _Value);
        if (_Error != std::errc{} || _Pointer != Value_.data() + Value_.size() || _Value == 0 || _Value > 1'000'000)
            throw std::invalid_argument("Excel 第 " + std::to_string(Row_) + " 行的生产数量必须为 1 到 1000000 的整数");
        return _Value;
    }

    Variant ParseParameterValue(
        const iCAX::TemplateRuntime::SParameterDefinition& Definition_,
        const std::string& Value_, const std::uint64_t Row_)
    {
        switch (Definition_.ValueType)
        {
        case EParameterValueType::Number:
        {
            char* _End = nullptr;
            const auto _Number = std::strtod(Value_.c_str(), &_End);
            if (_End != Value_.c_str() + Value_.size() || !std::isfinite(_Number))
                throw std::invalid_argument("Excel 第 " + std::to_string(Row_) + " 行的“" + Definition_.DisplayName.Resolve("zh-CN") + "”必须为数字");
            return Variant(_Number);
        }
        case EParameterValueType::Integer:
        {
            char* _End = nullptr;
            const auto _Number = std::strtod(Value_.c_str(), &_End);
            if (_End != Value_.c_str() + Value_.size() || !std::isfinite(_Number)
                || _Number < 0 || std::floor(_Number) != _Number
                || _Number > static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
            {
                throw std::invalid_argument("Excel 第 " + std::to_string(Row_) + " 行的“" + Definition_.DisplayName.Resolve("zh-CN") + "”必须为非负整数");
            }
            return Variant(static_cast<unsigned long long>(_Number));
        }
        case EParameterValueType::Boolean:
            if (Value_ == "1" || Value_ == "true" || Value_ == "TRUE" || Value_ == "是" || Value_ == "启用") return Variant(true);
            if (Value_ == "0" || Value_ == "false" || Value_ == "FALSE" || Value_ == "否" || Value_ == "关闭") return Variant(false);
            throw std::invalid_argument("Excel 第 " + std::to_string(Row_) + " 行的“" + Definition_.DisplayName.Resolve("zh-CN") + "”必须为是/否");
        case EParameterValueType::String:
        case EParameterValueType::Enumeration:
            return Variant(Value_);
        }
        throw std::invalid_argument("Excel 参数类型不受支持");
    }
}

std::filesystem::path iCAX::TubeDesigner::WriteBatchExcelTemplate(
    const std::filesystem::path& TemplatePath_, const STemplateDescriptor& Descriptor_, const std::vector<SBatchExcelColumn>& Columns_)
{
    if (TemplatePath_.empty() || TemplatePath_.extension() != ".xlsx") throw std::invalid_argument("Excel 导入工作簿必须使用 .xlsx 扩展名");
    if (Columns_.empty()) throw std::invalid_argument("Excel 导入模板至少需要一列");
    if (std::filesystem::exists(TemplatePath_)) throw std::invalid_argument("Excel 导入模板已存在，不能覆盖");
    VariantArray _Columns;
    std::vector<std::string> _Headers;
    std::vector<STableWorkbookValidation> _Validations;
    std::unordered_map<std::string, const iCAX::TemplateRuntime::SParameterDefinition*> _Parameters;
    for (const auto& _Parameter : Descriptor_.Parameters) _Parameters.emplace(_Parameter.Key, &_Parameter);
    _Headers.reserve(Columns_.size());
    for (std::size_t _Index = 0; _Index < Columns_.size(); ++_Index)
    {
        auto _Column = Columns_[_Index];
        if (_Column.Key.empty() || _Column.Title.empty()) throw std::invalid_argument("Excel 导入列缺少参数键或列名");
        if (const auto _Definition = _Parameters.find(_Column.Key); _Definition != _Parameters.end())
        {
            _Column.Choices.clear();
            std::set<std::string> _Labels;
            for (const auto& _Choice : _Definition->second->Choices)
            {
                const auto _Value = ParameterText(_Choice.Value);
                auto _Label = _Choice.DisplayName.Resolve("zh-CN");
                if (_Label.empty()) _Label = _Value;
                if (!_Labels.insert(_Label).second)
                    throw std::invalid_argument("Excel 中文枚举选项重复：" + _Column.Title + " / " + _Label);
                _Column.Choices.push_back({ _Choice.Value, _Label, ChoiceValueType(_Choice.Value) });
            }
            if (!_Column.Choices.empty())
            {
                std::vector<std::string> _LabelsForExcel;
                for (const auto& _Choice : _Column.Choices) _LabelsForExcel.push_back(_Choice.Label);
                _Validations.push_back({ _Index, std::move(_LabelsForExcel) });
            }
            else if (_Definition->second->ValueType == EParameterValueType::Boolean)
            {
                _Validations.push_back({ _Index, { "是", "否" } });
            }
        }
        ObjectMap _ColumnMetadata{{ "key", _Column.Key }, { "title", _Column.Title },
            { "required", _Column.Required }, { "defaultValue", _Column.DefaultValue }};
        if (!_Column.Choices.empty())
        {
            VariantArray _Choices;
            for (const auto& _Choice : _Column.Choices)
                _Choices.emplace_back(ObjectMap{{ "value", _Choice.Value }, { "label", _Choice.Label },
                    { "valueType", _Choice.ValueType }});
            _ColumnMetadata.emplace("choices", std::move(_Choices));
        }
        _Columns.emplace_back(std::move(_ColumnMetadata));
        _Headers.push_back(_Column.Title);
    }
    const ObjectMap _Definition{
        { "schema", std::string("icax.tube-designer.batch-excel") }, { "schemaVersion", 1ull },
        { "templateId", Descriptor_.ID }, { "templateVersion", Descriptor_.Version },
        { "templateName", Descriptor_.DisplayName.Resolve("zh-CN") }, { "headerRow", 2ull },
        { "columns", std::move(_Columns) }
    };
    if (!TemplatePath_.parent_path().empty()) std::filesystem::create_directories(TemplatePath_.parent_path());
    try
    {
        // This first row is deliberately visible in Excel.  The same identity
        // is also stored in the hidden contract sheet for machine validation,
        // but a human opening a template must be able to identify its product.
        WriteTableWorkbook(TemplatePath_,
            "批量导入工作簿：" + Descriptor_.DisplayName.Resolve("zh-CN"),
            _Headers, {},
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Variant(_Definition)),
            _Validations);
    }
    catch (...)
    {
        std::error_code _Ignored; std::filesystem::remove(TemplatePath_, _Ignored); throw;
    }
    return TemplatePath_;
}

iCAX::TubeDesigner::SBatchExcelDefinition iCAX::TubeDesigner::ReadBatchExcelDefinition(
    const std::filesystem::path& WorkbookPath_)
{
    if (WorkbookPath_.extension() != ".xlsx") throw std::invalid_argument("批量产品导入目前只支持 .xlsx 工作簿");
    const auto _MetadataSheet = ReadWorksheet(WorkbookPath_, "xl/worksheets/sheet2.xml");
    if (_MetadataSheet.empty() || _MetadataSheet.front().empty() || _MetadataSheet.front().front().empty())
        throw std::invalid_argument("Excel 文件不是由 TubeDesigner 批量导入工作簿生成，缺少内置列定义");
    const auto _Definition = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_MetadataSheet.front().front());
    if (!_Definition.Is<ObjectMap>()) throw std::invalid_argument("Excel 内置列定义格式无效");
    const auto _Object = _Definition.To<ObjectMap>();
    if (OptionalText(_Object, "schema") != "icax.tube-designer.batch-excel") throw std::invalid_argument("不是 TubeDesigner Excel 导入模板");
    const auto _ColumnsValue = _Object.find("columns");
    if (_ColumnsValue == _Object.end() || !_ColumnsValue->second.Is<VariantArray>()) throw std::invalid_argument("Excel 内置列定义缺少列配置");
    std::vector<SBatchExcelColumn> _Columns;
    for (const auto& _ColumnValue : _ColumnsValue->second.To<VariantArray>())
    {
        if (!_ColumnValue.Is<ObjectMap>()) throw std::invalid_argument("Excel 内置列配置无效");
        const auto _Column = _ColumnValue.To<ObjectMap>();
        _Columns.push_back({ RequiredText(_Column, "key"), RequiredText(_Column, "title"),
            OptionalBool(_Column, "required"), OptionalText(_Column, "defaultValue"), OptionalChoices(_Column) });
    }
    return { RequiredText(_Object, "templateId"), OptionalText(_Object, "templateVersion"), OptionalText(_Object, "templateName"), std::move(_Columns) };
}

iCAX::TubeDesigner::SBatchExcelImport iCAX::TubeDesigner::ReadBatchExcelImport(
    const std::filesystem::path& WorkbookPath_, const STemplateDescriptor& Descriptor_)
{
    const auto _Definition = ReadBatchExcelDefinition(WorkbookPath_);
    if (_Definition.TemplateID != Descriptor_.ID)
        throw std::invalid_argument("Excel 导入定义与当前产品模板不一致");
    const auto& _ExpectedVersion = _Definition.TemplateVersion;
    if (!_ExpectedVersion.empty() && _ExpectedVersion != Descriptor_.Version)
        throw std::invalid_argument("Excel 导入定义对应的产品模板版本已变化，请重新导出模板");
    std::unordered_map<std::string, const iCAX::TemplateRuntime::SParameterDefinition*> _Parameters;
    for (const auto& _Parameter : Descriptor_.Parameters) _Parameters.emplace(_Parameter.Key, &_Parameter);
    const auto _Sheet = ReadWorksheet(WorkbookPath_);
    if (_Sheet.size() < 2) throw std::invalid_argument("Excel 工作簿缺少标题行和列标题");
    std::unordered_map<std::string, std::size_t> _HeaderIndex;
    for (std::size_t _Index = 0; _Index < _Sheet[1].size(); ++_Index) if (!_Sheet[1][_Index].empty()) _HeaderIndex.emplace(_Sheet[1][_Index], _Index);
    for (const auto& _Column : _Definition.Columns) if (!_HeaderIndex.contains(_Column.Title)) throw std::invalid_argument("Excel 缺少列：" + _Column.Title);
    SBatchExcelImport _Result{ _Definition.TemplateID, _Definition.TemplateVersion, _Definition.TemplateName };
    for (std::size_t _Index = 2; _Index < _Sheet.size(); ++_Index)
    {
        const auto& _Cells = _Sheet[_Index]; const auto _RowNumber = static_cast<std::uint64_t>(_Index + 1);
        bool _HasData = false; for (const auto& _Cell : _Cells) if (! _Cell.empty()) { _HasData = true; break; }
        if (!_HasData) continue;
        SBatchExcelImportRow _Row; _Row.SourceRow = _RowNumber;
        for (const auto& _Column : _Definition.Columns)
        {
            const auto _Position = _HeaderIndex.at(_Column.Title); const auto _Cell = _Position < _Cells.size() ? _Cells[_Position] : std::string();
            const auto _Value = _Cell.empty() ? _Column.DefaultValue : _Cell;
            if (_Column.Required && _Value.empty()) throw std::invalid_argument("Excel 第 " + std::to_string(_RowNumber) + " 行的“" + _Column.Title + "”不能为空");
            if (_Column.Key == "__instanceName") _Row.InstanceName = _Value;
            else if (_Column.Key == "__instanceQuantity") _Row.InstanceQuantity = ParseQuantity(_Value.empty() ? "1" : _Value, _RowNumber);
            else if (!_Value.empty())
            {
                const auto _Definition = _Parameters.find(_Column.Key);
                if (_Definition == _Parameters.end())
                    throw std::invalid_argument("Excel 模板包含当前产品模板没有的参数：" + _Column.Key);
                _Row.Parameters[_Column.Key] = _Column.Choices.empty()
                    ? ParseParameterValue(*_Definition->second, _Value, _RowNumber)
                    : ResolveChoiceValue(_Column, _Value, _RowNumber);
            }
        }
        _Result.Rows.push_back(std::move(_Row));
    }
    return _Result;
}
