#include "pch.h"

#include <ApplicationContext/IApplicationContext.h>
#include <Data/VariantSerializer.h>
#include <ProjectContext/ISceneContext.h>
#include <SDO/SDORegistrationCatalog.h>
#include <TubeDesigner/TubeDesigner.h>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;

    class CStandardPartTestApplication final : public iCAX::Application::IApplicationContext
    {
    public:
        const iCAX::Application::CApplicationDescriptor& GetDescriptor() const override { return m_Descriptor; }
        const iCAX::Application::CApplicationPaths& GetPaths() const override { return m_Paths; }
        iCAX::Data::PropertyBag GetSettings() const override { return {}; }
        const iCAX::Services::CServiceProvider& Services() const override
        { throw std::logic_error("Unexpected application service access"); }
    private:
        iCAX::Application::CApplicationDescriptor m_Descriptor;
        iCAX::Application::CApplicationPaths m_Paths;
    };

    // Invalid requests must fail before reading or changing any project state.
    class CStandardPartReadBoundaryScene final : public iCAX::Project::ISceneContext
    {
    public:
        mutable int DatabaseRequests = 0;
        const iCAX::Data::uuid& GetSceneID() const override { return m_ID; }
        const iCAX::Data::uuid& GetSceneChannelID() const override { return m_ID; }
        const std::string& GetSceneName() const override { return m_Name; }
        iCAX::Interaction::CSDOEndpoint GetBackendSDOEndpoint() const override { return {}; }
        iCAX::Interaction::CSDOEndpoint GetFrontendSDOEndpoint() const override { return {}; }
        bool IsMainScene() const override { return true; }
        bool IsTransientScene() const override { return true; }
        iCAX::Database::IRepository& Database() override
        { ++DatabaseRequests; throw std::runtime_error("standard-part-test: validated geometry reached database"); }
        const iCAX::Database::IRepository& Database() const override
        { ++DatabaseRequests; throw std::runtime_error("Unexpected const database access"); }
        iCAX::Resource::CResourceLibrary& Resources() override
        { throw std::logic_error("Unexpected resource access"); }
        const iCAX::Resource::CResourceLibrary& Resources() const override
        { throw std::logic_error("Unexpected const resource access"); }
        bool HasPDOHub() const override { return false; }
        iCAX::PDO::IPDOHub& PDOHub() override { throw std::logic_error("Unexpected PDO access"); }
        const iCAX::PDO::IPDOHub& PDOHub() const override { throw std::logic_error("Unexpected const PDO access"); }
        iCAX::Services::CServiceProvider& Services() const override
        { throw std::logic_error("Unexpected scene service access"); }
    private:
        iCAX::Data::uuid m_ID;
        std::string m_Name = "Standard part validation";
    };

    std::shared_ptr<iCAX::Interaction::ISDO> StandardPartSDO()
    {
        // Force the product DLL to load and replay its real registration.
        (void)iCAX::TubeDesigner::GetTubeDesignerContractVersion();
        iCAX::Interaction::CSDORegistry _Registry;
        iCAX::Interaction::CSDORegistrationCatalog::ReplayAll(_Registry);
        const auto _Method = iCAX::Interaction::MakeSDOMethod("TubeDesigner", "AddNestingStandardPart");
        auto _SDO = _Registry.Find(_Method.nSDOCode);
        if (!_SDO || !_SDO->HasMethod(_Method.nMethodCode))
            throw std::runtime_error("AddNestingStandardPart is not registered");
        return _SDO;
    }

    iCAX::Interaction::CInvocation StandardPartRequest(const ObjectMap& Payload_)
    {
        iCAX::Interaction::CInvocation _Request;
        _Request.nCallID = 1;
        _Request.Method = iCAX::Interaction::MakeSDOMethod("TubeDesigner", "AddNestingStandardPart");
        const auto _Text = iCAX::Data::VariantSerializer::Serialize(Payload_);
        _Request.Payload.assign(_Text.begin(), _Text.end());
        return _Request;
    }

    ObjectMap StandardPartDxf()
    {
        return {
            { "schema", std::string("icax.imported-tube-profile") }, { "schemaVersion", 1ull },
            { "kind", std::string("imported-dxf") }, { "name", std::string("DXF rectangle") },
            { "width", 40.0 }, { "depth", 20.0 },
            { "contours", VariantArray{ObjectMap{
                { "kind", std::string("roundedRectangle") },
                { "width", 40.0 }, { "height", 20.0 }, { "radius", 0.0 } }} }
        };
    }
}

TEST(TubeDesignerStandardPartSDO, RegistersAndRejectsInvalidSourcesBeforeTouchingTheScene)
{
    auto _SDO = StandardPartSDO();
    CStandardPartTestApplication _Application;
    CStandardPartReadBoundaryScene _Scene;
    const auto _Reject = [&](const ObjectMap& _Payload) {
        EXPECT_THROW(_SDO->Invoke(StandardPartRequest(_Payload), _Application, nullptr, nullptr, &_Scene),
            std::invalid_argument);
        EXPECT_EQ(0, _Scene.DatabaseRequests);
    };
    _Reject({ { "length", 1000.0 }, { "profileRef", ObjectMap{
        { "scope", std::string("template") }, { "id", std::string("round") },
        { "templateId", std::string("test-template") } } } });
    _Reject({ { "length", 1000.0 }, { "profile", StandardPartDxf() },
        { "profileRef", ObjectMap{{ "scope", std::string("system") }, { "id", std::string("round") }} } });
    _Reject({ { "length", 1000.0 }, { "profile", StandardPartDxf() },
        { "parameters", ObjectMap{{ "width", 50.0 }} } });
    auto _Profile = StandardPartDxf();
    _Profile["kind"] = std::string("parametric-package");
    _Reject({ { "length", 1000.0 }, { "profile", _Profile } });
    _Profile = StandardPartDxf();
    _Profile["contours"] = VariantArray{};
    _Reject({ { "length", 1000.0 }, { "profile", _Profile } });
    for (const auto _Length : { -1.0, 0.0, 0.99, 100001.0 })
        _Reject({ { "length", _Length }, { "profile", StandardPartDxf() } });
    EXPECT_THROW(_SDO->Invoke(StandardPartRequest({}), _Application, nullptr, nullptr, nullptr),
        std::invalid_argument);
}

TEST(TubeDesignerStandardPartSDO, BuildsFrozenDxfWithoutAProfileLibraryBeforeOpeningTheDatabase)
{
    auto _SDO = StandardPartSDO();
    CStandardPartTestApplication _Application;
    for (const auto _Length : { 1.0, 1000.0, 100000.0 })
    {
        CStandardPartReadBoundaryScene _Scene;
        try
        {
            (void)_SDO->Invoke(StandardPartRequest({ { "length", _Length }, { "profile", StandardPartDxf() } }),
                _Application, nullptr, nullptr, &_Scene);
            FAIL() << "Test scene must stop persistence";
        }
        catch (const std::runtime_error& _Error)
        {
            EXPECT_EQ("standard-part-test: validated geometry reached database", std::string(_Error.what()));
            EXPECT_EQ(1, _Scene.DatabaseRequests);
        }
    }
}

TEST(TubeDesignerPunchLayoutSDO, RejectsMalformedExplicitLayoutsBeforeRuntimeOrSceneMutation)
{
    const auto _SDO = StandardPartSDO();
    CStandardPartTestApplication _Application;
    const auto _Method = iCAX::Interaction::MakeSDOMethod("TubeDesigner", "PreviewPunchWizard");
    ASSERT_TRUE(_SDO->HasMethod(_Method.nMethodCode));
    const std::vector<ObjectMap> _Invalid{
        {{"arrayOffsets", VariantArray{}}},
        {{"arrayOffsets", std::string("0, 100")}},
        {{"arrayCount", 2ull}, {"arrayOffsets", VariantArray{0.}}},
        {{"arrayCount", 2ull}, {"arrayOffsets", VariantArray{0., 0.}}},
        {{"arrayOffsets", VariantArray{true}}},
        {{"rowCount", 2ull}, {"rowOffsets", VariantArray{0., 360.}}, {"face", std::string("round")}},
        {{"skippedInstances", VariantArray{1ull}}},
        {{"skippedInstances", VariantArray{std::string("0:1")}}},
        {{"skippedInstances", VariantArray{std::string("0:0"), std::string("0:0")}}},
        {{"layoutDatum", std::string("unrecognised")}}
    };
    for (const auto& _Feature : _Invalid) {
        CStandardPartReadBoundaryScene _Scene;
        auto _Request = StandardPartRequest({{"length", 1000.0}, {"profile", StandardPartDxf()},
            {"features", VariantArray{_Feature}}});
        _Request.Method = _Method;
        EXPECT_THROW(_SDO->Invoke(_Request, _Application, nullptr, nullptr, &_Scene), std::invalid_argument);
        EXPECT_EQ(0, _Scene.DatabaseRequests);
    }
}
