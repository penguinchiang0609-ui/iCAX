#include <atomic>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace python_host_boundary_acceptance {
using namespace iCAX::TemplateRuntime;
using iCAX::Data::ObjectMap;
using iCAX::Data::Variant;
using iCAX::Data::VariantArray;

ObjectMap& object(Variant& value) { return std::get<ObjectMap>(value.m_Value); }
const ObjectMap& object(const Variant& value) { return std::get<ObjectMap>(value.m_Value); }
VariantArray& array(Variant& value) { return std::get<VariantArray>(value.m_Value); }

class CScriptFixture final {
public:
    CScriptFixture()
    {
        static std::atomic<unsigned long long> next{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto directory = std::filesystem::current_path() / "output/tests/python-host-boundary"
            / (std::to_string(stamp) + "-" + std::to_string(next.fetch_add(1)));
        std::filesystem::create_directories(directory);
        Path = directory / "template.py";
        std::ofstream script(Path, std::ios::binary);
        script << R"python(import struct

def generate(parameters, context):
    mode = parameters.get('mode', 'echo')
    if mode == 'raise':
        raise ValueError('deliberate boundary failure')
    if mode == 'nan':
        return {'value': float('nan')}
    if mode == 'set':
        return {'value': {1, 2}}
    if mode == 'cycle':
        value = {}
        value['self'] = value
        return value
    if mode == 'mutate':
        context['values']['note']['label'] = 'changed by Python'
    return {
        'payload': context['values'],
        'floats': context.get('floats', []),
        'bits': [struct.pack('>d', value).hex() for value in context.get('floats', [])],
        'tuple': (None, True, 7, 'tuple value'),
    }
)python";
        if (!script) throw std::runtime_error("cannot write Python host boundary fixture");
    }

    ObjectMap Request(ObjectMap Context_, const std::string& Mode_ = "echo") const
    {
        return ObjectMap{{"protocol", std::string(kTemplateProtocol)},
            {"protocolVersion", static_cast<unsigned long long>(kTemplateProtocolVersion)},
            {"operation", std::string("evaluate")}, {"templatePath", Path.string()},
            {"template", ObjectMap{{"id", std::string("test.python-host-boundary")},
                {"version", std::string("1.0.0")}, {"packageDigest", Path.string()},
                {"parameters", VariantArray{}}, {"extensions", ObjectMap{}}}},
            {"parameters", ObjectMap{{"mode", Mode_}}}, {"context", std::move(Context_)}};
    }

private:
    std::filesystem::path Path;
};

ObjectMap context()
{
    return ObjectMap{{"values", ObjectMap{
        {"emptyObject", ObjectMap{}}, {"emptyArray", VariantArray{}}, {"null", Variant{}},
        {"boolean", true}, {"signedMin", (std::numeric_limits<long long>::min)()},
        {"signedMax", (std::numeric_limits<long long>::max)()},
        {"unsignedMax", (std::numeric_limits<unsigned long long>::max)()},
        {"utf8", std::string("\xE7\xAE\xA1\xE6\x9D\x90\n\"\\")},
        {"note", ObjectMap{{"label", std::string("source")}, {"values", NumberArray({1, 2, 3})}}},
        {"largeText", std::string(256 * 1024, 'q')}}}};
}

TEST(TemplateRuntimeTest, StandardJsonBorrowedContainersMatchVariantEncodingAndRejectInvalidValues)
{
    using namespace iCAX::TemplateRuntime;
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    const VariantArray values{std::nextafter(1.0, 2.0), -0.0,
        (std::numeric_limits<unsigned long long>::max)(), std::string("UTF-8 \xE7\x94\xA8\xE6\x96\x99"),
        ObjectMap{{"nested", VariantArray{true, Variant{}, 3.25}}}};
    const ObjectMap document{{"values", values}, {"escaped", std::string("line\n\"quoted\"")}};
    const auto before = CStandardJsonCodec::Serialize(Variant(document));
    EXPECT_EQ(before, CStandardJsonCodec::Serialize(document));
    EXPECT_EQ(CStandardJsonCodec::Serialize(Variant(values)), CStandardJsonCodec::Serialize(values));
    EXPECT_EQ(before, CStandardJsonCodec::Serialize(document));
    const auto parsed = CStandardJsonCodec::Parse(CStandardJsonCodec::Serialize(document));
    EXPECT_EQ(before, CStandardJsonCodec::Serialize(parsed));
    const ObjectMap nonfinite{{"nested", VariantArray{std::numeric_limits<double>::infinity()}}};
    const VariantArray nan{ObjectMap{{"invalid", std::numeric_limits<double>::quiet_NaN()}}};
    EXPECT_THROW(CStandardJsonCodec::Serialize(nonfinite), std::invalid_argument);
    EXPECT_THROW(CStandardJsonCodec::Serialize(nan), std::invalid_argument);
}

std::string doubleBits(double value)
{
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << std::bit_cast<unsigned long long>(value);
    return text.str();
}
}

TEST(TemplateRuntimeTest, PythonHostBoundaryPreservesBinaryNumbersAndOwnsNestedValues)
{
    using namespace python_host_boundary_acceptance;
    CScriptFixture fixture;
    CPythonTemplateHost host(EmbeddedPythonHostOptions(std::filesystem::current_path()));
    const std::vector<double> doubles{std::nextafter(1.0, 2.0), std::nextafter(-1.0, 0.0), -0.0,
        (std::numeric_limits<double>::denorm_min)(), (std::numeric_limits<double>::max)(),
        0.7071067811865476, -0.8660254037844387};
    auto inputContext = context();
    VariantArray floatValues;
    for (const auto value : doubles) floatValues.emplace_back(value);
    inputContext["floats"] = std::move(floatValues);
    auto request = fixture.Request(inputContext);
    const auto original = request;
    std::vector<std::string> roles{"previous"};
    auto result = host.Invoke(request, &roles);
    EXPECT_TRUE(roles.empty());
    EXPECT_EQ(original, request);
    auto wireContext = CStandardJsonCodec::Parse(CStandardJsonCodec::Serialize(Variant(inputContext)));
    EXPECT_EQ(object(wireContext).at("values"), result.at("payload"));
    auto& returnedFloats = array(result.at("floats"));
    auto& returnedBits = array(result.at("bits"));
    ASSERT_EQ(doubles.size(), returnedFloats.size()); ASSERT_EQ(doubles.size(), returnedBits.size());
    for (std::size_t index = 0; index < doubles.size(); ++index)
    {
        EXPECT_EQ(doubleBits(doubles[index]), doubleBits(returnedFloats[index].To<double>()));
        EXPECT_EQ(doubleBits(doubles[index]), returnedBits[index].To<std::string>());
    }
    EXPECT_TRUE(std::signbit(returnedFloats[2].To<double>()));
    ASSERT_EQ(4u, array(result.at("tuple")).size());
    EXPECT_TRUE(array(result.at("tuple"))[0].Is<std::monostate>());
    EXPECT_TRUE(array(result.at("tuple"))[1].To<bool>());
    auto mutated = host.Invoke(fixture.Request(inputContext, "mutate"));
    EXPECT_EQ("changed by Python", object(object(mutated.at("payload")).at("note")).at("label").To<std::string>());
    EXPECT_EQ("source", object(object(inputContext.at("values")).at("note")).at("label").To<std::string>());
    request.clear(); inputContext.clear(); wireContext = Variant{};
    EXPECT_EQ(std::string(256 * 1024, 'q'), object(result.at("payload")).at("largeText").To<std::string>());
    EXPECT_EQ("source", object(object(result.at("payload")).at("note")).at("label").To<std::string>());
    object(object(result.at("payload")).at("note"))["label"] = std::string("native result changed");
    EXPECT_EQ("source", object(object(object(original.at("context")).at("values")).at("note")).at("label").To<std::string>());
}

TEST(TemplateRuntimeTest, PythonHostBoundaryRejectsInvalidValuesAndRecoversAfterExceptions)
{
    using namespace python_host_boundary_acceptance;
    CScriptFixture fixture;
    CPythonTemplateHost host(EmbeddedPythonHostOptions(std::filesystem::current_path()));
    for (const auto mode : {"raise", "nan", "set", "cycle"})
    {
        std::vector<std::string> roles{"unchanged on failure"};
        EXPECT_THROW(host.Invoke(fixture.Request(context(), mode), &roles), std::runtime_error) << mode;
        EXPECT_EQ((std::vector<std::string>{"unchanged on failure"}), roles);
        EXPECT_EQ("source", object(object(host.Invoke(fixture.Request(context())).at("payload")).at("note")).at("label").To<std::string>());
    }
    for (const auto invalid : {(std::numeric_limits<double>::infinity)(),
        -(std::numeric_limits<double>::infinity)(), (std::numeric_limits<double>::quiet_NaN)()})
    {
        auto inputContext = context(); object(inputContext.at("values"))["invalid"] = invalid;
        EXPECT_THROW(host.Invoke(fixture.Request(inputContext)), std::invalid_argument);
        EXPECT_EQ("source", object(object(host.Invoke(fixture.Request(context())).at("payload")).at("note")).at("label").To<std::string>());
    }
    host.Stop(); EXPECT_FALSE(host.IsRunning());
    auto restarted = host.Invoke(fixture.Request(context()));
    EXPECT_TRUE(host.IsRunning());
    EXPECT_EQ("source", object(object(restarted.at("payload")).at("note")).at("label").To<std::string>());
}

TEST(TemplateRuntimeTest, PythonHostBoundaryRepeatedCallsKeepIndependentEnvelopesAndResults)
{
    using namespace python_host_boundary_acceptance;
    CScriptFixture fixture;
    CPythonTemplateHost host(EmbeddedPythonHostOptions(std::filesystem::current_path()));
    auto first = host.Invoke(fixture.Request(context()));
    for (unsigned long long index = 0; index < 12; ++index)
    {
        auto inputContext = context(); object(inputContext.at("values"))["call"] = index;
        const auto original = inputContext;
        std::vector<std::string> roles{"previous"};
        auto result = host.Invoke(fixture.Request(inputContext), &roles);
        EXPECT_TRUE(roles.empty()); EXPECT_EQ(original, inputContext);
        EXPECT_EQ(static_cast<long long>(index), object(result.at("payload")).at("call").To<long long>());
        object(object(result.at("payload")).at("note"))["label"] = std::string("changed returned value");
        EXPECT_EQ("source", object(object(first.at("payload")).at("note")).at("label").To<std::string>());
    }
}
