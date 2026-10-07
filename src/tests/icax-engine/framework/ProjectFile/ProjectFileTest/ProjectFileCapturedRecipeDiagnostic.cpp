#include "pch.h"

#include <gtest/gtest.h>
#include <Data/VariantSerializer.h>
#include <ProjectFile/ProjectFile.h>
#include <TemplateRuntime/StandardJsonCodec.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>

namespace
{
    std::string ReadDiagnosticEnvironment(const char* Name_)
    {
        char* _Buffer = nullptr;
        size_t _Size = 0;
        if (_dupenv_s(&_Buffer, &_Size, Name_) != 0)
            throw std::runtime_error("Cannot read diagnostic environment");
        const std::unique_ptr<char, decltype(&std::free)> _Value(_Buffer, &std::free);
        return _Buffer ? std::string(_Buffer) : std::string{};
    }
}

// This diagnostic consumes the captured native GenerationRun property subset,
// with source JSON float types restored where the source and captured values
// are identical. Its byte counts use the real native typed serializer. It is
// deliberately separate from OCC/shape evaluation and the live test scenes.
TEST(ProjectFileCodec, DISABLED_CapturedManufacturingRecipePersistenceDiagnostic)
{
    using namespace iCAX::Data;
    using namespace iCAX::ProjectFile;
    using iCAX::TemplateRuntime::CStandardJsonCodec;
    const auto _FixtureName = ReadDiagnosticEnvironment("ICAX_PROJECT_FILE_CAPTURED_PROPERTIES");
    ASSERT_FALSE(_FixtureName.empty()) << "Set ICAX_PROJECT_FILE_CAPTURED_PROPERTIES for the real captured recipe diagnostic";
    const auto _ReportName = ReadDiagnosticEnvironment("ICAX_PROJECT_FILE_DIAGNOSTIC_REPORT");
    ASSERT_FALSE(_ReportName.empty());
    std::ifstream _Input(_FixtureName, std::ios::binary);
    ASSERT_TRUE(_Input.good());
    const std::string _FixtureText((std::istreambuf_iterator<char>(_Input)), {});
    auto _Properties = CStandardJsonCodec::Parse(_FixtureText).To<ObjectMap>();
    ASSERT_EQ(_Properties.size(), 7u);
    ObjectMap _FieldBytes;
    for (const auto& [_Name, _Value] : _Properties)
        _FieldBytes[_Name] = static_cast<unsigned long long>(VariantSerializer::Serialize(_Value).size());
    const auto _PropertiesBytes = VariantSerializer::Serialize(Variant(_Properties)).size();
    ASSERT_GT(_PropertiesBytes, 64ull * 1024ull * 1024ull);
    ASSERT_LT(_PropertiesBytes, 256ull * 1024ull * 1024ull);

    const auto _Project = uuid::from_string("a1000000-0000-4000-8000-000000000001").value();
    const auto _Scene = uuid::from_string("a1000000-0000-4000-8000-000000000002").value();
    const auto _Run = uuid::from_string("a1000000-0000-4000-8000-000000000003").value();
    CProjectDocument _Document;
    _Document.Info.Magic = "ICAX_TUBE_DESIGNER";
    _Document.Info.ProductID = "icax.tube-designer";
    _Document.Info.FormatVersion = "0.1";
    _Document.Info.nFormatRevision = 1;
    _Document.Info.ProjectID = _Project;
    _Document.Info.MainSceneID = _Scene;
    _Document.Info.ProjectName = "captured round-scene GenerationRun persistence";
    _Document.Entities = {{_Project}, {_Scene}, {_Run}};
    _Document.Components.push_back({_Run, "CGenerationRunComponent", true, std::move(_Properties)});
    _Document.Canonicalize();
    const auto _Bytes = CProjectFileCodec::Encode(_Document, EProjectFileEncoding::Binary);
    const bool _ExpectOldLimit = !ReadDiagnosticEnvironment("ICAX_PROJECT_FILE_EXPECT_OLD_LIMIT").empty();
    ObjectMap _Report{
        {"componentClass", std::string("CGenerationRunComponent")},
        {"field", std::string("Component.Properties")},
        {"capturedPropertyCount", static_cast<unsigned long long>(7)},
        {"fixtureStandardJsonBytes", static_cast<unsigned long long>(_FixtureText.size())},
        {"nativeTypedFieldBytes", _FieldBytes},
        {"nativeTypedPropertiesBytes", static_cast<unsigned long long>(_PropertiesBytes)},
        {"encodedProjectBytes", static_cast<unsigned long long>(_Bytes.size())},
        {"oldReaderLimitBytes", static_cast<unsigned long long>(64ull * 1024ull * 1024ull)},
        {"componentPropertiesReaderLimitBytes", static_cast<unsigned long long>((_ExpectOldLimit ? 64ull : 256ull) * 1024ull * 1024ull)},
        {"measurementBasis", std::string("native VariantSerializer of captured seven-property subset; source-matched JSON numeric types restored; omitted small fields and resource bodies are not counted")},
        {"fixture", std::string(_FixtureName)}};
    const auto _WriteReport = [&]() {
        std::ofstream _Output(_ReportName, std::ios::binary | std::ios::trunc);
        _Output << CStandardJsonCodec::Serialize(Variant(_Report)) << '\n';
        if (!_Output.good()) throw std::runtime_error("Cannot write captured persistence diagnostic report");
    };
    if (_ExpectOldLimit)
    {
        try
        {
            CProjectFileCodec::Decode(_Bytes);
            _Report["expectedOldReaderRejection"] = false;
            _WriteReport();
            FAIL() << "Old reader must reproduce the captured component failure";
        }
        catch (const std::invalid_argument& _Error)
        {
            _Report["expectedOldReaderRejection"] = true;
            _Report["error"] = std::string(_Error.what());
            _WriteReport();
            EXPECT_STREQ(_Error.what(), "Binary project string is too large");
        }
        return;
    }
    const auto _Decoded = CProjectFileCodec::Decode(_Bytes);
    ASSERT_EQ(_Decoded.Document, _Document);
    const auto _Reencoded = CProjectFileCodec::Encode(_Decoded.Document, EProjectFileEncoding::Binary);
    ASSERT_EQ(_Reencoded, _Bytes);
    const auto _Path = std::filesystem::path(_ReportName).parent_path() /
        ("captured-generation-run-" + to_string(GenerateNewUUID()) + ".ictd");
    CProjectFileCodec::WriteAtomic(_Path, _Document, EProjectFileEncoding::Binary);
    ASSERT_EQ(CProjectFileCodec::Read(_Path).Document, _Document);
    std::ifstream _SavedInput(_Path, std::ios::binary);
    const std::vector<uint8_t> _SavedBytes((std::istreambuf_iterator<char>(_SavedInput)), {});
    ASSERT_EQ(_SavedBytes, _Bytes);
    _Report["typedDocumentRoundTripEqual"] = true;
    _Report["reencodedBytesEqual"] = true;
    _Report["realAtomicSaveAndReadEqual"] = true;
    _Report["savedProject"] = _Path.string();
    _Report["savedProjectBytes"] = static_cast<unsigned long long>(_SavedBytes.size());
    _WriteReport();
    std::cout << "Captured Component.Properties typed bytes=" << _PropertiesBytes
        << ", saved project bytes=" << _SavedBytes.size() << std::endl;
}
