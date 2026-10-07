#include <functional>

namespace template_codec_ownership_acceptance {
using namespace iCAX::TemplateRuntime;
using iCAX::Data::ObjectMap;
using iCAX::Data::Variant;
using iCAX::Data::VariantArray;

ObjectMap& object(Variant& value) { return std::get<ObjectMap>(value.m_Value); }
VariantArray& array(Variant& value) { return std::get<VariantArray>(value.m_Value); }

ObjectMap display()
{
    auto profile = RoundedProfile(40.0, 20.0, 3.0);
    // A sizeable opaque geometry argument exposes accidental aliasing/lifetime
    // dependence without evaluating or claiming a synthetic BRep is valid.
    profile["ownershipPayload"] = std::string(64 * 1024, 'x');
    ObjectMap properties{{"partKind", std::string("tube")},
        {"note", ObjectMap{{"label", std::string("source")}, {"values", NumberArray({1, 2, 3})}}}};
    ObjectMap item{{"key", std::string("part.0001")},
        {"displayName", ObjectMap{{"zh-CN", std::string("Owned part")}}},
        {"geometry", ObjectMap{{"resource", std::string("part")}}},
        {"properties", std::move(properties)}};
    VariantArray resources{
        ObjectMap{{"key", std::string("profile")}, {"operator", std::string("profile2d")},
            {"inputs", VariantArray{}}, {"arguments", std::move(profile)}},
        ObjectMap{{"key", std::string("part")}, {"operator", std::string("extrude")},
            {"inputs", VariantArray{std::string("profile")}},
            {"arguments", ObjectMap{{"vector", NumberArray({0.0, 0.0, 100.0})}}}}};
    return ObjectMap{{"schema", std::string(kDisplayModelSchema)}, {"schemaVersion", 2ull},
        {"coordinateSystem", std::string("right-handed-x-width-y-depth-z-height")},
        {"lengthUnit", std::string("mm")}, {"resources", std::move(resources)},
        {"items", VariantArray{std::move(item)}},
        {"roots", VariantArray{std::string("part.0001")}}, {"annotations", VariantArray{}}};
}

ObjectMap declaration()
{
    return ObjectMap{{"schema", std::string(kManufacturingModelSchema)}, {"schemaVersion", 4ull},
        {"connections", VariantArray{}}, {"processes", VariantArray{}}};
}

ObjectMap generatedDefinition()
{
    auto result = CTemplateCodec::ComposeManufacturingModel(declaration(), display());
    auto& item = object(array(result.at("items")).front());
    item.erase("geometry");
    item["designInput"] = ObjectMap{{"kind", std::string("tube-member")},
        {"profileResource", std::string("profile")}, {"start", NumberArray({0, 0, 0})},
        {"end", NumberArray({0, 0, 100})},
        {"sectionFrame", ObjectMap{{"origin", NumberArray({0, 0, 0})},
            {"xAxis", NumberArray({1, 0, 0})}, {"yAxis", NumberArray({0, 1, 0})},
            {"zAxis", NumberArray({0, 0, 1})}}}};
    object(item.at("properties"))["tubeDesigner.profile"] = ObjectMap{{"sectionResource", std::string("profile")}};
    ObjectMap source{{"scope", std::string("design")},
        {"itemKeys", VariantArray{std::string("part.0001")}}};
    ObjectMap processInput{{"schema", std::string("icax.assembly-process-input")},
        {"schemaVersion", 3ull}, {"geometry", ObjectMap{}},
        {"parts", ObjectMap{{"source", std::move(source)}}}};
    ObjectMap outputs{{"stock", ObjectMap{{"key", std::string("stock")},
        {"kind", std::string("manufacturing-set")}}}};
    ObjectMap processDefinition{{"templateId", std::string("test.generated-stock")},
        {"processInput", std::move(processInput)}, {"parameters", ObjectMap{}},
        {"processDrafts", ObjectMap{}}, {"dependencies", VariantArray{}},
        {"outputs", std::move(outputs)}};
    result["processes"] = VariantArray{ObjectMap{{"key", std::string("allocate")},
        {"kind", std::string("assembly-process")}, {"definition", std::move(processDefinition)}}};
    return result;
}

ObjectMap generatedExecution()
{
    auto result = CTemplateCodec::AdaptManufacturingInputModel(
        CTemplateCodec::ComposeManufacturingModel(declaration(), display()));
    object(array(result.at("items")).front())["key"] = std::string("stock.0001");
    object(array(result.at("outputs")).front())["items"] = VariantArray{std::string("stock.0001")};
    auto& extensions = object(result.at("extensions"));
    extensions["tubeDesigner.manufacturingPartCount"] = 1ull;
    extensions["tubeDesigner.assemblyOutputSets"] = VariantArray{ObjectMap{
        {"processKey", std::string("allocate")}, {"outputKey", std::string("stock")},
        {"key", std::string("stock")}, {"kind", std::string("manufacturing-set")},
        {"itemKeys", VariantArray{std::string("stock.0001")}},
        {"sourceItemKeys", VariantArray{std::string("part.0001")}}}};
    return result;
}

void equivalent(const SNeutralModel& left, const SNeutralModel& right)
{
    EXPECT_EQ(left.TemplateID, right.TemplateID); EXPECT_EQ(left.TemplateVersion, right.TemplateVersion);
    EXPECT_EQ(left.PackageDigest, right.PackageDigest); EXPECT_EQ(left.CoordinateSystem, right.CoordinateSystem);
    EXPECT_EQ(left.LengthUnit, right.LengthUnit); EXPECT_EQ(left.Parameters, right.Parameters);
    EXPECT_EQ(left.Extensions, right.Extensions);
    ASSERT_EQ(left.Geometry.size(), right.Geometry.size());
    for (std::size_t index = 0; index < left.Geometry.size(); ++index)
    {
        EXPECT_EQ(left.Geometry[index].Key, right.Geometry[index].Key);
        EXPECT_EQ(left.Geometry[index].Operator, right.Geometry[index].Operator);
        EXPECT_EQ(left.Geometry[index].Inputs, right.Geometry[index].Inputs);
        EXPECT_EQ(left.Geometry[index].Arguments, right.Geometry[index].Arguments);
    }
    ASSERT_EQ(left.Items.size(), right.Items.size());
    for (std::size_t index = 0; index < left.Items.size(); ++index)
    {
        EXPECT_EQ(left.Items[index].Key, right.Items[index].Key);
        EXPECT_EQ(left.Items[index].DisplayName.Default, right.Items[index].DisplayName.Default);
        EXPECT_EQ(left.Items[index].DisplayName.Translations, right.Items[index].DisplayName.Translations);
        EXPECT_EQ(left.Items[index].Representations, right.Items[index].Representations);
        EXPECT_EQ(left.Items[index].Children, right.Items[index].Children);
        EXPECT_EQ(left.Items[index].Properties, right.Items[index].Properties);
    }
    ASSERT_EQ(left.Outputs.size(), right.Outputs.size());
    for (std::size_t index = 0; index < left.Outputs.size(); ++index)
    {
        EXPECT_EQ(left.Outputs[index].Key, right.Outputs[index].Key);
        EXPECT_EQ(left.Outputs[index].Purpose, right.Outputs[index].Purpose);
        EXPECT_EQ(left.Outputs[index].ItemKeys, right.Outputs[index].ItemKeys);
        EXPECT_EQ(left.Outputs[index].Properties, right.Outputs[index].Properties);
    }
    // These fixtures intentionally carry no extra presentation tables/diagnostics.
    EXPECT_TRUE(left.Relationships.empty()); EXPECT_TRUE(right.Relationships.empty());
    EXPECT_TRUE(left.Tables.empty()); EXPECT_TRUE(right.Tables.empty());
    EXPECT_TRUE(left.Diagnostics.empty()); EXPECT_TRUE(right.Diagnostics.empty());
}

template<typename TWork>
std::string error(TWork&& work)
{
    try { work(); ADD_FAILURE() << "codec unexpectedly accepted an invalid document"; }
    catch (const std::invalid_argument& failure) { return failure.what(); }
    return {};
}
}

TEST(TemplateRuntimeTest, ObjectMapCodecEntrypointsMatchVariantWithoutMutatingInputs)
{
    using namespace template_codec_ownership_acceptance;
    auto design = display();
    auto definition = declaration();
    const auto originalDesign = design, originalDefinition = definition;
    auto input = CTemplateCodec::ComposeManufacturingModel(definition, design);
    EXPECT_EQ(input, CTemplateCodec::ComposeManufacturingModel(Variant(definition), Variant(design)));
    const auto originalInput = input;
    auto displayAdapter = CTemplateCodec::AdaptDisplayModel(design);
    EXPECT_EQ(displayAdapter, CTemplateCodec::AdaptDisplayModel(Variant(design)));
    auto manufacturingAdapter = CTemplateCodec::AdaptManufacturingInputModel(input);
    EXPECT_EQ(manufacturingAdapter, CTemplateCodec::AdaptManufacturingInputModel(Variant(input)));
    EXPECT_EQ(manufacturingAdapter, CTemplateCodec::AdaptManufacturingModel(input));
    EXPECT_EQ(manufacturingAdapter, CTemplateCodec::AdaptManufacturingModel(Variant(input)));
    auto expanded = CTemplateCodec::ExpandNeutralModelResources(manufacturingAdapter);
    EXPECT_EQ(expanded, CTemplateCodec::ExpandNeutralModelResources(Variant(manufacturingAdapter)));
    EXPECT_EQ(expanded, CTemplateCodec::ExpandNeutralModelResources(expanded));
    for (const auto* document : {&design, &input, &displayAdapter, &manufacturingAdapter, &expanded})
        equivalent(CTemplateCodec::ParseNeutralModel(*document), CTemplateCodec::ParseNeutralModel(Variant(*document)));
    auto versionTwoInput = input; versionTwoInput["schemaVersion"] = 2ull;
    EXPECT_EQ(CTemplateCodec::AdaptManufacturingInputModel(versionTwoInput), CTemplateCodec::AdaptManufacturingInputModel(Variant(versionTwoInput)));
    equivalent(CTemplateCodec::ParseNeutralModel(versionTwoInput), CTemplateCodec::ParseNeutralModel(Variant(versionTwoInput)));
    EXPECT_EQ(originalDesign, design); EXPECT_EQ(originalDefinition, definition); EXPECT_EQ(originalInput, input);
}

TEST(TemplateRuntimeTest, ObjectMapCodecReturnsIndependentlyOwnedNestedData)
{
    using namespace template_codec_ownership_acceptance;
    auto design = display();
    auto definition = declaration();
    const auto originalDesign = design;
    auto composed = CTemplateCodec::ComposeManufacturingModel(definition, design);
    auto adapted = CTemplateCodec::AdaptDisplayModel(design);
    auto expanded = CTemplateCodec::ExpandNeutralModelResources(adapted);
    auto parsed = CTemplateCodec::ParseNeutralModel(design);
    object(object(array(composed.at("items")).front()).at("properties"))["note"] = std::string("changed result");
    object(array(adapted.at("resources")).front())["arguments"] = ObjectMap{};
    object(array(expanded.at("geometry")).front())["arguments"] = ObjectMap{};
    object(parsed.Items.front().Properties.at("note"))["label"] = std::string("changed parsed result");
    EXPECT_EQ(originalDesign, design);
    auto owned = CTemplateCodec::ParseNeutralModel(design);
    design.clear(); definition.clear(); composed.clear(); adapted.clear(); expanded.clear();
    ASSERT_EQ(2u, owned.Geometry.size()); ASSERT_EQ(1u, owned.Items.size());
    EXPECT_EQ(std::string(64 * 1024, 'x'), owned.Geometry.front().Arguments.at("ownershipPayload").To<std::string>());
    EXPECT_EQ("source", object(owned.Items.front().Properties.at("note")).at("label").To<std::string>());
    EXPECT_EQ("part", owned.Items.front().Representations.at("result"));
}

TEST(TemplateRuntimeTest, ValidatedManufacturingParseMatchesCompleteValidationAndOwnsItsResult)
{
    using namespace template_codec_ownership_acceptance;
    auto design = display();
    auto definition = declaration();
    auto input = CTemplateCodec::ComposeManufacturingModel(definition, design);
    auto execution = CTemplateCodec::AdaptManufacturingInputModel(input);
    const auto originalExecution = execution;
    const Variant designValue(design);
    EXPECT_NO_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(Variant(definition), Variant(execution), &designValue));
    EXPECT_NO_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(definition, execution, &design));
    auto owned = CTemplateCodec::ValidateAndParseManufacturingExecutionModel(definition, execution, &design);
    equivalent(owned, CTemplateCodec::ParseNeutralModel(execution));
    equivalent(owned, CTemplateCodec::ValidateAndParseManufacturingExecutionModel(input, execution));
    EXPECT_EQ(originalExecution, execution);
    definition.clear(); design.clear(); input.clear(); execution.clear();
    EXPECT_EQ(std::string(64 * 1024, 'x'), owned.Geometry.front().Arguments.at("ownershipPayload").To<std::string>());
    EXPECT_EQ("part.0001", owned.Outputs.front().ItemKeys.front());
}

TEST(TemplateRuntimeTest, ValidatedManufacturingParseRetainsGraphIdentityAndProvenanceRejections)
{
    using namespace template_codec_ownership_acceptance;
    auto input = CTemplateCodec::ComposeManufacturingModel(declaration(), display());
    auto fixed = CTemplateCodec::AdaptManufacturingInputModel(input);
    std::vector<ObjectMap> invalidFixed;
    auto missingGeometry = fixed; array(missingGeometry.at("resources")).pop_back(); invalidFixed.push_back(std::move(missingGeometry));
    auto changedIdentity = fixed;
    object(array(changedIdentity.at("items")).front())["key"] = std::string("other");
    object(array(changedIdentity.at("outputs")).front())["items"] = VariantArray{std::string("other")};
    invalidFixed.push_back(std::move(changedIdentity));
    auto undeclaredOutput = fixed;
    object(undeclaredOutput.at("extensions"))["tubeDesigner.assemblyOutputSets"] = VariantArray{ObjectMap{}};
    invalidFixed.push_back(std::move(undeclaredOutput));
    for (const auto& execution : invalidFixed)
    {
        const auto expected = error([&] { CTemplateCodec::ValidateManufacturingExecutionModel(Variant(input), Variant(execution)); });
        ASSERT_FALSE(expected.empty());
        EXPECT_EQ(expected, error([&] { CTemplateCodec::ValidateManufacturingExecutionModel(input, execution); }));
        EXPECT_EQ(expected, error([&] { (void)CTemplateCodec::ValidateAndParseManufacturingExecutionModel(input, execution); }));
    }
    auto generated = generatedDefinition();
    auto actual = generatedExecution();
    ASSERT_EQ(3ull, generated.at("schemaVersion").To<unsigned long long>());
    auto& generatedProcess = object(array(generated.at("processes")).front());
    ASSERT_EQ(3ull, object(object(generatedProcess.at("definition")).at("processInput"))
        .at("schemaVersion").To<unsigned long long>());
    const auto originalDefinition = generated, originalExecution = actual;
    EXPECT_NO_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(Variant(generated), Variant(actual)));
    equivalent(CTemplateCodec::ValidateAndParseManufacturingExecutionModel(generated, actual), CTemplateCodec::ParseNeutralModel(actual));
    std::vector<ObjectMap> invalidGenerated;
    auto wrongSources = actual;
    object(array(object(wrongSources.at("extensions")).at("tubeDesigner.assemblyOutputSets")).front())["sourceItemKeys"] = VariantArray{std::string("other-design")};
    invalidGenerated.push_back(std::move(wrongSources));
    auto wrongCount = actual; object(wrongCount.at("extensions"))["tubeDesigner.manufacturingPartCount"] = 2ull; invalidGenerated.push_back(std::move(wrongCount));
    auto missingSets = actual; object(missingSets.at("extensions")).erase("tubeDesigner.assemblyOutputSets"); invalidGenerated.push_back(std::move(missingSets));
    auto undeclaredSet = actual;
    object(array(object(undeclaredSet.at("extensions")).at("tubeDesigner.assemblyOutputSets")).front())["outputKey"] = std::string("foreign");
    invalidGenerated.push_back(std::move(undeclaredSet));
    for (const auto& execution : invalidGenerated)
    {
        const auto expected = error([&] { CTemplateCodec::ValidateManufacturingExecutionModel(Variant(generated), Variant(execution)); });
        ASSERT_FALSE(expected.empty());
        EXPECT_EQ(expected, error([&] { CTemplateCodec::ValidateManufacturingExecutionModel(generated, execution); }));
        EXPECT_EQ(expected, error([&] { (void)CTemplateCodec::ValidateAndParseManufacturingExecutionModel(generated, execution); }));
    }
    EXPECT_EQ(originalDefinition, generated); EXPECT_EQ(originalExecution, actual);
}

TEST(TemplateRuntimeTest, ObjectMapCodecPreservesVariantTypeErrorsAndValidationOrder)
{
    using namespace template_codec_ownership_acceptance;
    const Variant wrong(VariantArray{});
    EXPECT_EQ("neutral model must be an object", error([&] { (void)CTemplateCodec::ParseNeutralModel(wrong); }));
    EXPECT_EQ("neutral model must be an object", error([&] { (void)CTemplateCodec::ExpandNeutralModelResources(wrong); }));
    EXPECT_EQ("display model must be an object", error([&] { (void)CTemplateCodec::AdaptDisplayModel(wrong); }));
    EXPECT_EQ("manufacturing model must be an object", error([&] { (void)CTemplateCodec::AdaptManufacturingModel(wrong); }));
    EXPECT_EQ("manufacturing input model must be an object", error([&] { (void)CTemplateCodec::AdaptManufacturingInputModel(wrong); }));
    EXPECT_EQ("manufacturing declaration must be an object", error([&] { (void)CTemplateCodec::ComposeManufacturingModel(wrong, wrong); }));
    EXPECT_EQ("manufacturing definition must be an object", error([&] { CTemplateCodec::ValidateManufacturingExecutionModel(wrong, wrong); }));
    const auto validDeclaration = declaration();
    EXPECT_EQ("manufacturing declaration execution requires shared design", error([&] { CTemplateCodec::ValidateManufacturingExecutionModel(Variant(validDeclaration), wrong); }));
    EXPECT_EQ("shared design model must be an object", error([&] { (void)CTemplateCodec::ComposeManufacturingModel(Variant(validDeclaration), wrong); }));
    auto invalidDeclaration = validDeclaration; invalidDeclaration["result"] = ObjectMap{};
    const auto message = error([&] { (void)CTemplateCodec::ComposeManufacturingModel(invalidDeclaration, display()); });
    EXPECT_EQ(message, error([&] { (void)CTemplateCodec::ComposeManufacturingModel(Variant(invalidDeclaration), wrong); }));
    auto invalidDisplay = display(); object(array(invalidDisplay.at("items")).front()).at("geometry") = ObjectMap{{"resource", std::string("missing")}};
    EXPECT_EQ(error([&] { (void)CTemplateCodec::AdaptDisplayModel(Variant(invalidDisplay)); }), error([&] { (void)CTemplateCodec::AdaptDisplayModel(invalidDisplay); }));
}
