#include "pch.h"

#include "PartListXlsxExporter.h"

#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace
{
    using iCAX::TubeDesigner::SPartListRow;

    struct SZipEntry final
    {
        std::string Name;
        std::string Data;
        std::uint32_t Crc = 0;
        std::uint32_t Offset = 0;
    };

    std::string EscapeXml(const std::string& Value_)
    {
        std::string _Result;
        _Result.reserve(Value_.size());
        for (const auto _Character : Value_)
        {
            switch (_Character)
            {
            case '&': _Result += "&amp;"; break;
            case '<': _Result += "&lt;"; break;
            case '>': _Result += "&gt;"; break;
            case '"': _Result += "&quot;"; break;
            case '\'': _Result += "&apos;"; break;
            default: _Result.push_back(_Character); break;
            }
        }
        return _Result;
    }

    std::string FormatNumber(double Value_)
    {
        if (!std::isfinite(Value_)) return "0";
        std::ostringstream _Stream;
        _Stream << std::fixed << std::setprecision(3) << Value_;
        auto _Text = _Stream.str();
        while (_Text.size() > 1 && _Text.back() == '0') _Text.pop_back();
        if (!_Text.empty() && _Text.back() == '.') _Text.pop_back();
        return _Text;
    }

    std::string TextCell(
        const std::string& Reference_, const std::string& Value_, std::uint32_t Style_)
    {
        return "<c r=\"" + Reference_ + "\" s=\"" + std::to_string(Style_)
            + "\" t=\"inlineStr\"><is><t xml:space=\"preserve\">"
            + EscapeXml(Value_) + "</t></is></c>";
    }

    std::string NumberCell(
        const std::string& Reference_, const std::string& Value_, std::uint32_t Style_)
    {
        return "<c r=\"" + Reference_ + "\" s=\"" + std::to_string(Style_)
            + "\"><v>" + Value_ + "</v></c>";
    }

    std::string BuildWorksheet(const std::vector<SPartListRow>& Rows_)
    {
        const auto _LastRow = static_cast<std::uint64_t>(Rows_.size()) + 2;
        std::vector<std::string> _MergeReferences{ "A1:Q1" };
        std::ostringstream _Rows;
        _Rows << "<row r=\"1\" ht=\"28\" customHeight=\"1\">"
            << TextCell("A1", "TubeDesigner 零件清单", 1) << "</row>";
        _Rows << "<row r=\"2\" ht=\"23\" customHeight=\"1\">";
        const std::array<std::string, 17> _Headers{
            "产品序号", "产品名称", "产品编码", "零件序号", "零件号", "零件名称",
            "管型 / 类别", "规格 / 板件宽×高×厚", "管材长度 (mm)", "数量", "文件名", "相对路径",
            "零件类型", "板宽 (mm)", "板高 (mm)", "板厚 (mm)", "材料"
        };
        for (std::size_t _Index = 0; _Index < _Headers.size(); ++_Index)
        {
            const std::string _Reference(1, static_cast<char>('A' + _Index));
            _Rows << TextCell(_Reference + "2", _Headers[_Index], 2);
        }
        _Rows << "</row>";

        std::size_t _Offset = 0;
        while (_Offset < Rows_.size())
        {
            const auto _ProductIndex = Rows_[_Offset].ProductIndex;
            auto _End = _Offset + 1;
            while (_End < Rows_.size() && Rows_[_End].ProductIndex == _ProductIndex) ++_End;
            const auto _FirstSheetRow = _Offset + 3;
            const auto _LastSheetRow = _End + 2;
            if (_LastSheetRow > _FirstSheetRow)
            {
                for (const auto _Column : { 'A', 'B', 'C' })
                {
                    _MergeReferences.push_back(
                        std::string(1, _Column) + std::to_string(_FirstSheetRow)
                        + ":" + std::string(1, _Column) + std::to_string(_LastSheetRow));
                }
            }

            for (auto _RowIndex = _Offset; _RowIndex < _End; ++_RowIndex)
            {
                const auto& _Row = Rows_[_RowIndex];
                const auto _SheetRow = _RowIndex + 3;
                const auto _RowText = std::to_string(_SheetRow);
                _Rows << "<row r=\"" << _SheetRow << "\" ht=\"20\" customHeight=\"1\">";
                if (_RowIndex == _Offset)
                {
                    _Rows << NumberCell("A" + _RowText, std::to_string(_Row.ProductIndex), 5)
                        << TextCell("B" + _RowText, _Row.ProductName, 3)
                        << TextCell("C" + _RowText, _Row.ProductCode, 3);
                }
                _Rows << NumberCell("D" + _RowText, std::to_string(_Row.PartIndex), 5)
                    << TextCell("E" + _RowText, _Row.PartNumber, 3)
                    << TextCell("F" + _RowText, _Row.PartName, 3)
                    << TextCell("G" + _RowText, _Row.ProfileDisplayName, 3)
                    << TextCell("H" + _RowText, _Row.ProfileSpecification, 3)
                    << (_Row.PartKind == "plate" ? TextCell("I" + _RowText, "", 4)
                        : NumberCell("I" + _RowText, FormatNumber(_Row.Length), 4))
                    << NumberCell("J" + _RowText, std::to_string(_Row.Quantity), 5)
                    << TextCell("K" + _RowText, _Row.FileName, 3)
                    << TextCell("L" + _RowText, _Row.RelativePath, 3)
                    << TextCell("M" + _RowText, _Row.PartKind == "plate" ? "板件" : "管材", 3)
                    << (_Row.PartKind == "plate" ? NumberCell("N" + _RowText, FormatNumber(_Row.PlateWidth), 4) : TextCell("N" + _RowText, "", 4))
                    << (_Row.PartKind == "plate" ? NumberCell("O" + _RowText, FormatNumber(_Row.PlateHeight), 4) : TextCell("O" + _RowText, "", 4))
                    << (_Row.PartKind == "plate" ? NumberCell("P" + _RowText, FormatNumber(_Row.PlateThickness), 4) : TextCell("P" + _RowText, "", 4))
                    << TextCell("Q" + _RowText, _Row.Material, 3)
                    << "</row>";
            }
            _Offset = _End;
        }

        std::ostringstream _Merges;
        _Merges << "<mergeCells count=\"" << _MergeReferences.size() << "\">";
        for (const auto& _Reference : _MergeReferences)
            _Merges << "<mergeCell ref=\"" << _Reference << "\"/>";
        _Merges << "</mergeCells>";

        std::ostringstream _Sheet;
        _Sheet << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            << "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            << "<dimension ref=\"A1:Q" << _LastRow << "\"/>"
            << "<sheetViews><sheetView tabSelected=\"1\" workbookViewId=\"0\">"
            << "<pane ySplit=\"2\" topLeftCell=\"A3\" activePane=\"bottomLeft\" state=\"frozen\"/>"
            << "<selection pane=\"bottomLeft\" activeCell=\"A3\" sqref=\"A3\"/>"
            << "</sheetView></sheetViews><sheetFormatPr defaultRowHeight=\"18\"/>"
            << "<cols>"
            << "<col min=\"1\" max=\"1\" width=\"10\" customWidth=\"1\"/>"
            << "<col min=\"2\" max=\"2\" width=\"24\" customWidth=\"1\"/>"
            << "<col min=\"3\" max=\"3\" width=\"18\" customWidth=\"1\"/>"
            << "<col min=\"4\" max=\"4\" width=\"10\" customWidth=\"1\"/>"
            << "<col min=\"5\" max=\"7\" width=\"18\" customWidth=\"1\"/>"
            << "<col min=\"8\" max=\"8\" width=\"25\" customWidth=\"1\"/>"
            << "<col min=\"9\" max=\"10\" width=\"12\" customWidth=\"1\"/>"
            << "<col min=\"11\" max=\"11\" width=\"24\" customWidth=\"1\"/>"
            << "<col min=\"12\" max=\"12\" width=\"38\" customWidth=\"1\"/>"
            << "<col min=\"13\" max=\"17\" width=\"14\" customWidth=\"1\"/>"
            << "</cols><sheetData>" << _Rows.str() << "</sheetData>"
            << "<autoFilter ref=\"D2:Q" << _LastRow << "\"/>" << _Merges.str()
            << "<pageMargins left=\"0.3\" right=\"0.3\" top=\"0.5\" bottom=\"0.5\" header=\"0.2\" footer=\"0.2\"/>"
            << "<pageSetup orientation=\"landscape\" fitToWidth=\"1\" fitToHeight=\"0\"/>"
            << "</worksheet>";
        return _Sheet.str();
    }

    std::uint32_t Crc32(const std::string& Data_)
    {
        std::uint32_t _Crc = 0xFFFFFFFFu;
        for (const auto _Character : Data_)
        {
            _Crc ^= static_cast<unsigned char>(_Character);
            for (auto _Bit = 0; _Bit < 8; ++_Bit)
                _Crc = (_Crc >> 1) ^ (0xEDB88320u & (0u - (_Crc & 1u)));
        }
        return ~_Crc;
    }

    void WriteUInt16(std::ostream& Stream_, std::uint16_t Value_)
    {
        const std::array<char, 2> _Bytes{
            static_cast<char>(Value_ & 0xFFu), static_cast<char>((Value_ >> 8) & 0xFFu)
        };
        Stream_.write(_Bytes.data(), _Bytes.size());
    }

    void WriteUInt32(std::ostream& Stream_, std::uint32_t Value_)
    {
        const std::array<char, 4> _Bytes{
            static_cast<char>(Value_ & 0xFFu), static_cast<char>((Value_ >> 8) & 0xFFu),
            static_cast<char>((Value_ >> 16) & 0xFFu), static_cast<char>((Value_ >> 24) & 0xFFu)
        };
        Stream_.write(_Bytes.data(), _Bytes.size());
    }

    std::uint32_t CheckedUInt32(std::uint64_t Value_, const char* Name_)
    {
        if (Value_ > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error(std::string("XLSX package exceeds ZIP32 ") + Name_ + " limit");
        return static_cast<std::uint32_t>(Value_);
    }

    void WriteZipArchive(const std::filesystem::path& Target_, std::vector<SZipEntry> Entries_)
    {
        if (Entries_.size() > std::numeric_limits<std::uint16_t>::max())
            throw std::runtime_error("XLSX package has too many ZIP entries");
        std::ofstream _Stream(Target_, std::ios::binary | std::ios::trunc);
        if (!_Stream) throw std::runtime_error("Could not create XLSX part list");

        for (auto& _Entry : Entries_)
        {
            _Entry.Crc = Crc32(_Entry.Data);
            _Entry.Offset = CheckedUInt32(static_cast<std::uint64_t>(_Stream.tellp()), "offset");
            const auto _DataSize = CheckedUInt32(_Entry.Data.size(), "entry size");
            const auto _NameSize = static_cast<std::uint16_t>(_Entry.Name.size());
            WriteUInt32(_Stream, 0x04034B50u);
            WriteUInt16(_Stream, 20);
            WriteUInt16(_Stream, 0x0800);
            WriteUInt16(_Stream, 0);
            WriteUInt16(_Stream, 0);
            WriteUInt16(_Stream, 0);
            WriteUInt32(_Stream, _Entry.Crc);
            WriteUInt32(_Stream, _DataSize);
            WriteUInt32(_Stream, _DataSize);
            WriteUInt16(_Stream, _NameSize);
            WriteUInt16(_Stream, 0);
            _Stream.write(_Entry.Name.data(), _Entry.Name.size());
            _Stream.write(_Entry.Data.data(), _Entry.Data.size());
        }

        const auto _CentralOffset = CheckedUInt32(
            static_cast<std::uint64_t>(_Stream.tellp()), "central directory offset");
        for (const auto& _Entry : Entries_)
        {
            const auto _DataSize = CheckedUInt32(_Entry.Data.size(), "entry size");
            const auto _NameSize = static_cast<std::uint16_t>(_Entry.Name.size());
            WriteUInt32(_Stream, 0x02014B50u);
            WriteUInt16(_Stream, 20);
            WriteUInt16(_Stream, 20);
            WriteUInt16(_Stream, 0x0800);
            WriteUInt16(_Stream, 0);
            WriteUInt16(_Stream, 0);
            WriteUInt16(_Stream, 0);
            WriteUInt32(_Stream, _Entry.Crc);
            WriteUInt32(_Stream, _DataSize);
            WriteUInt32(_Stream, _DataSize);
            WriteUInt16(_Stream, _NameSize);
            WriteUInt16(_Stream, 0);
            WriteUInt16(_Stream, 0);
            WriteUInt16(_Stream, 0);
            WriteUInt16(_Stream, 0);
            WriteUInt32(_Stream, 0);
            WriteUInt32(_Stream, _Entry.Offset);
            _Stream.write(_Entry.Name.data(), _Entry.Name.size());
        }
        const auto _CentralEnd = CheckedUInt32(
            static_cast<std::uint64_t>(_Stream.tellp()), "central directory end");
        const auto _EntryCount = static_cast<std::uint16_t>(Entries_.size());
        WriteUInt32(_Stream, 0x06054B50u);
        WriteUInt16(_Stream, 0);
        WriteUInt16(_Stream, 0);
        WriteUInt16(_Stream, _EntryCount);
        WriteUInt16(_Stream, _EntryCount);
        WriteUInt32(_Stream, _CentralEnd - _CentralOffset);
        WriteUInt32(_Stream, _CentralOffset);
        WriteUInt16(_Stream, 0);
        _Stream.flush();
        if (!_Stream) throw std::runtime_error("Could not finish XLSX part list");
    }

    std::vector<SZipEntry> BuildWorkbookEntries(const std::vector<SPartListRow>& Rows_)
    {
        return {
            { "[Content_Types].xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
                "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
                "</Types>" },
            { "_rels/.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
                "</Relationships>" },
            { "xl/workbook.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                "<bookViews><workbookView xWindow=\"0\" yWindow=\"0\" windowWidth=\"22000\" windowHeight=\"12000\"/></bookViews>"
                "<sheets><sheet name=\"零件清单\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
                "<calcPr calcId=\"191029\"/></workbook>" },
            { "xl/_rels/workbook.xml.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
                "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
                "</Relationships>" },
            { "xl/styles.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
                "<fonts count=\"3\">"
                "<font><sz val=\"10\"/><name val=\"Microsoft YaHei\"/><family val=\"2\"/></font>"
                "<font><b/><color rgb=\"FFFFFFFF\"/><sz val=\"10\"/><name val=\"Microsoft YaHei\"/><family val=\"2\"/></font>"
                "<font><b/><color rgb=\"FFFFFFFF\"/><sz val=\"15\"/><name val=\"Microsoft YaHei\"/><family val=\"2\"/></font>"
                "</fonts><fills count=\"4\">"
                "<fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>"
                "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF178F82\"/><bgColor indexed=\"64\"/></patternFill></fill>"
                "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF21313A\"/><bgColor indexed=\"64\"/></patternFill></fill>"
                "</fills><borders count=\"2\"><border/><border>"
                "<left style=\"thin\"><color rgb=\"FFD8E0E4\"/></left><right style=\"thin\"><color rgb=\"FFD8E0E4\"/></right>"
                "<top style=\"thin\"><color rgb=\"FFD8E0E4\"/></top><bottom style=\"thin\"><color rgb=\"FFD8E0E4\"/></bottom>"
                "</border></borders><cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
                "<cellXfs count=\"6\">"
                "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
                "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"3\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
                "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" wrapText=\"1\"/></xf>"
                "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\"><alignment vertical=\"center\" wrapText=\"1\"/></xf>"
                "<xf numFmtId=\"4\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyAlignment=\"1\"><alignment horizontal=\"right\" vertical=\"center\"/></xf>"
                "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
                "</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
                "</styleSheet>" },
            { "xl/worksheets/sheet1.xml", BuildWorksheet(Rows_) }
        };
    }
}

void iCAX::TubeDesigner::WritePartListWorkbook(
    const std::filesystem::path& TargetPath_,
    const std::vector<SPartListRow>& Rows_)
{
    if (TargetPath_.empty()) throw std::invalid_argument("XLSX target path is required");
    if (Rows_.empty()) throw std::invalid_argument("XLSX part list requires at least one row");
    if (!TargetPath_.parent_path().empty())
        std::filesystem::create_directories(TargetPath_.parent_path());

    auto _TemporaryPath = TargetPath_;
    _TemporaryPath += ".tmp";
    std::error_code _Ignored;
    std::filesystem::remove(_TemporaryPath, _Ignored);
    try
    {
        WriteZipArchive(_TemporaryPath, BuildWorkbookEntries(Rows_));
        std::filesystem::remove(TargetPath_, _Ignored);
        std::error_code _RenameError;
        std::filesystem::rename(_TemporaryPath, TargetPath_, _RenameError);
        if (_RenameError)
            throw std::runtime_error("Could not publish XLSX part list: " + _RenameError.message());
    }
    catch (...)
    {
        std::filesystem::remove(_TemporaryPath, _Ignored);
        throw;
    }
}

void iCAX::TubeDesigner::WriteTableWorkbook(
    const std::filesystem::path& TargetPath_,
    const std::string& Title_,
    const std::vector<std::string>& Headers_,
    const std::vector<std::vector<STableWorkbookCell>>& Rows_)
{
    if (TargetPath_.empty() || Headers_.empty() || Headers_.size() > 26 || Rows_.empty())
        throw std::invalid_argument("XLSX table requires a target, 1-26 columns and data rows");
    if (Rows_.size() > 1048574)
        throw std::invalid_argument("XLSX table exceeds worksheet row limit");
    if (std::filesystem::exists(TargetPath_))
        throw std::invalid_argument("XLSX target already exists; refusing to overwrite");
    if (!TargetPath_.parent_path().empty())
        std::filesystem::create_directories(TargetPath_.parent_path());

    const std::string _LastColumn(1, static_cast<char>('A' + Headers_.size() - 1));
    const auto _LastRow = std::to_string(Rows_.size() + 2);
    std::ostringstream _Rows;
    _Rows << "<row r=\"1\" ht=\"28\" customHeight=\"1\">"
        << TextCell("A1", Title_, 1) << "</row><row r=\"2\" ht=\"30\" customHeight=\"1\">";
    for (std::size_t _Column = 0; _Column < Headers_.size(); ++_Column)
        _Rows << TextCell(std::string(1, static_cast<char>('A' + _Column)) + "2", Headers_[_Column], 2);
    _Rows << "</row>";
    for (std::size_t _Index = 0; _Index < Rows_.size(); ++_Index)
    {
        const auto& _Row = Rows_[_Index];
        if (_Row.size() != Headers_.size()) throw std::invalid_argument("XLSX table row width is inconsistent");
        const auto _RowNumber = std::to_string(_Index + 3);
        _Rows << "<row r=\"" << _RowNumber << "\" ht=\"24\" customHeight=\"1\">";
        for (std::size_t _Column = 0; _Column < _Row.size(); ++_Column)
        {
            const auto _Reference = std::string(1, static_cast<char>('A' + _Column)) + _RowNumber;
            if (const auto _Number = std::get_if<double>(&_Row[_Column]))
            {
                if (!std::isfinite(*_Number)) throw std::invalid_argument("XLSX table contains a nonfinite number");
                _Rows << NumberCell(_Reference, FormatNumber(*_Number), 4);
            }
            else _Rows << TextCell(_Reference, std::get<std::string>(_Row[_Column]), 3);
        }
        _Rows << "</row>";
    }
    std::ostringstream _Sheet;
    _Sheet << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        << "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        << "<dimension ref=\"A1:" << _LastColumn << _LastRow << "\"/>"
        << "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"2\" topLeftCell=\"A3\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews>"
        << "<sheetFormatPr defaultRowHeight=\"24\"/><cols><col min=\"1\" max=\"" << Headers_.size()
        << "\" width=\"20\" customWidth=\"1\"/></cols><sheetData>" << _Rows.str() << "</sheetData>"
        << "<autoFilter ref=\"A2:" << _LastColumn << _LastRow << "\"/>"
        << "<mergeCells count=\"1\"><mergeCell ref=\"A1:" << _LastColumn << "1\"/></mergeCells>"
        << "<pageMargins left=\"0.3\" right=\"0.3\" top=\"0.5\" bottom=\"0.5\" header=\"0.2\" footer=\"0.2\"/>"
        << "<pageSetup orientation=\"landscape\" fitToWidth=\"1\" fitToHeight=\"0\"/></worksheet>";
    auto _Entries = BuildWorkbookEntries({});
    for (auto& _Entry : _Entries)
    {
        if (_Entry.Name == "xl/worksheets/sheet1.xml") _Entry.Data = _Sheet.str();
        if (_Entry.Name == "xl/workbook.xml")
        {
            const auto _Name = _Entry.Data.find("name=\"零件清单\"");
            if (_Name != std::string::npos) _Entry.Data.replace(_Name, std::string("name=\"零件清单\"").size(), "name=\"下料清单\"");
        }
    }
    auto _TemporaryPath = TargetPath_;
    _TemporaryPath += ".tmp";
    if (std::filesystem::exists(_TemporaryPath))
        throw std::invalid_argument("XLSX temporary target already exists; refusing to overwrite");
    try
    {
        WriteZipArchive(_TemporaryPath, std::move(_Entries));
        if (std::filesystem::exists(TargetPath_))
            throw std::runtime_error("XLSX destination appeared during export; refusing to overwrite");
        std::filesystem::rename(_TemporaryPath, TargetPath_);
    }
    catch (...)
    {
        std::error_code _Ignored;
        std::filesystem::remove(_TemporaryPath, _Ignored);
        throw;
    }
}
