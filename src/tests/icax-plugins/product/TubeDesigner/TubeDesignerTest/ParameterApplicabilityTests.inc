namespace parameter_applicability_acceptance {
using iCAX::Data::ObjectMap;
using iCAX::Data::Variant;
using iCAX::TemplateRuntime::CTemplateCodec;

auto descriptor()
{
    return CTemplateCodec::ParseDescriptor(iCAX::TemplateRuntime::CStandardJsonCodec::Parse(R"json({
      "schema":"icax.template-descriptor","schemaVersion":1,
      "id":"test.parameter-applicability","version":"1","displayName":"Applicability",
      "parameters":[
        {"key":"amount","displayName":"Amount","valueType":"number","defaultValue":2,
          "constraints":{"minimum":1,"maximum":5},
          "visibleWhen":{"op":"eq","parameter":"on","value":true}},
        {"key":"count","displayName":"Count","valueType":"integer","defaultValue":2,
          "constraints":{"minimum":1,"maximum":5},
          "enabledWhen":{"op":"eq","parameter":"on","value":true}},
        {"key":"text","displayName":"Text","valueType":"string","defaultValue":"good",
          "constraints":{"minimumLength":2,"maximumLength":5},
          "visibleWhen":{"op":"all","conditions":[
            {"op":"eq","parameter":"on","value":true},
            {"op":"any","conditions":[
              {"op":"ne","parameter":"mode","value":"off"},
              {"op":"not","condition":{"op":"eq","parameter":"mode","value":"off"}}
            ]}
          ]}},
        {"key":"never","displayName":"Never","valueType":"number","defaultValue":2,
          "constraints":{"minimum":1},"presentation":{"visible":false}},
        {"key":"choice","displayName":"Choice","valueType":"enum","defaultValue":"a",
          "choices":[{"value":"a","displayName":"A"},{"value":"b","displayName":"B"}],
          "visibleWhen":{"op":"eq","parameter":"on","value":true}},
        {"key":"mode","displayName":"Mode","valueType":"enum","defaultValue":"off",
          "choices":[{"value":"off","displayName":"Off"},{"value":"on","displayName":"On"}]},
        {"key":"on","displayName":"On","valueType":"boolean","defaultValue":false}
      ]
    })json"));
}
}

TEST(ParameterApplicability, InactiveDraftBoundsDoNotBlockAndParentOrderDoesNotMatter)
{
    using namespace parameter_applicability_acceptance;
    const auto schema = descriptor();
    const ObjectMap input{{"amount", -3.0}, {"count", 99LL}, {"text", std::string("")},
                          {"never", -7.0}, {"choice", std::string("b")}, {"on", false}};
    const auto before = input;
    const auto actual = CTemplateCodec::ValidateAndNormalizeParameters(schema, input);
    EXPECT_EQ(-3.0, actual.at("amount").To<double>());
    EXPECT_EQ(99LL, actual.at("count").To<long long>());
    EXPECT_EQ(std::string(""), actual.at("text").To<std::string>());
    EXPECT_EQ(-7.0, actual.at("never").To<double>());
    EXPECT_EQ(std::string("b"), actual.at("choice").To<std::string>());
    EXPECT_EQ(input, before);
    auto active = input; active["on"] = true;
    EXPECT_THROW(CTemplateCodec::ValidateAndNormalizeParameters(schema, active), std::invalid_argument);
    EXPECT_EQ(input, before);
}

TEST(ParameterApplicability, ActiveBoundsAndCompoundConditionsStillRejectInvalidValues)
{
    using namespace parameter_applicability_acceptance;
    const auto schema = descriptor();
    const ObjectMap valid{{"on", true}, {"mode", std::string("on")}, {"amount", 2.0},
                          {"count", 2LL}, {"text", std::string("good")}, {"never", -9.0}};
    EXPECT_NO_THROW(CTemplateCodec::ValidateAndNormalizeParameters(schema, valid));
    for (const auto& [key, value] : ObjectMap{{"amount", 6.0}, {"count", 0LL}, {"text", std::string("x")}})
    {
        auto bad = valid; bad[key] = value;
        EXPECT_THROW(CTemplateCodec::ValidateAndNormalizeParameters(schema, bad), std::invalid_argument);
    }
    auto hiddenText = valid; hiddenText["mode"] = std::string("off"); hiddenText["text"] = std::string("x");
    EXPECT_EQ(std::string("x"), CTemplateCodec::ValidateAndNormalizeParameters(schema, hiddenText).at("text").To<std::string>());
}

TEST(ParameterApplicability, InactiveFieldsStillRequireFiniteTypedValuesAndDeclaredChoices)
{
    using namespace parameter_applicability_acceptance;
    const auto schema = descriptor();
    for (const auto& [key, value] : ObjectMap{{"amount", std::numeric_limits<double>::infinity()},
            {"count", 0.5}, {"text", true}, {"choice", std::string("unknown")}, {"unexpected", 1LL}})
    {
        const ObjectMap input{{key, value}, {"on", false}};
        EXPECT_THROW(CTemplateCodec::ValidateAndNormalizeParameters(schema, input), std::invalid_argument);
    }
}

TEST(ParameterApplicability, InactiveMissingRequiredChildDoesNotInventOrRequireDraft)
{
    using namespace parameter_applicability_acceptance;
    auto schema = descriptor();
    schema.Parameters.front().DefaultValue = Variant{};
    const auto result = CTemplateCodec::ValidateAndNormalizeParameters(schema, ObjectMap{{"on", false}});
    EXPECT_FALSE(result.contains("amount"));
    EXPECT_THROW(CTemplateCodec::ValidateAndNormalizeParameters(schema, ObjectMap{{"on", true}}), std::invalid_argument);
}
