#include <gtest/gtest.h>

#include <Data/Array1.h>
#include <Data/Array2.h>
#include <Data/CommonFunction.h>
#include <Data/PropertyBag.h>
#include <Data/StableId.h>
#include <Data/VariantSerializer.h>
#include <Data/uuid.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace iCAX::Data;

TEST(DataArrayTest, Array1SupportsDefaultVectorAndArithmetic)
{
    Int3 values(std::vector<int>{ 1, 2 });

    EXPECT_EQ(1, values[0]);
    EXPECT_EQ(2, values[1]);
    EXPECT_EQ(2, values[2]);

    Int3 added = values + Int3(1, 1, 1);

    EXPECT_EQ(2, added[0]);
    EXPECT_EQ(3, added[1]);
    EXPECT_EQ(3, added[2]);
}

TEST(DataArrayTest, Array1EmptyVectorFillsDefaultValues)
{
    Int3 values(std::vector<int>{});

    EXPECT_EQ(0, values[0]);
    EXPECT_EQ(0, values[1]);
    EXPECT_EQ(0, values[2]);
}

TEST(DataArrayTest, Array2SupportsDefaultVectorAndScalarArithmetic)
{
    Int2x2 values(std::vector<int>{ 1, 2, 3 });

    EXPECT_EQ(1, values(0, 0));
    EXPECT_EQ(2, values(0, 1));
    EXPECT_EQ(3, values(1, 0));
    EXPECT_EQ(3, values(1, 1));

    Int2x2 doubled = values * 2;

    EXPECT_EQ(2, doubled(0, 0));
    EXPECT_EQ(4, doubled(0, 1));
    EXPECT_EQ(6, doubled(1, 0));
    EXPECT_EQ(6, doubled(1, 1));
}

TEST(DataArrayTest, Array2EmptyVectorFillsDefaultValues)
{
    Int2x2 values(std::vector<int>{});

    EXPECT_EQ(0, values(0, 0));
    EXPECT_EQ(0, values(0, 1));
    EXPECT_EQ(0, values(1, 0));
    EXPECT_EQ(0, values(1, 1));
}

TEST(DataVariantTest, VariantStoresExactTypes)
{
    Variant number(42);
    Variant text(std::string("value"));

    EXPECT_TRUE(number.Is<int>());
    EXPECT_EQ(42, number.To<int>());
    EXPECT_TRUE(text.Is<std::string>());
    EXPECT_EQ("value", text.To<std::string>());
    EXPECT_THROW(number.To<std::string>(), std::bad_variant_access);
}

TEST(DataVariantTest, VariantCanSetAndGetNestedPath)
{
    Variant root;

    root.SetByPath("items[0].name", Variant(std::string("bolt")));
    root.SetByPath("items[0].count", Variant(12));

    auto name = root.GetByPath("items[0].name");
    auto count = root.GetByPath("items[0].count");
    auto missing = root.GetByPath("items[1].name");

    ASSERT_TRUE(name.has_value());
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ("bolt", name->To<std::string>());
    EXPECT_EQ(12, count->To<int>());
    EXPECT_FALSE(missing.has_value());
}

TEST(DataVariantTest, FilterByPathAndPredicateReturnsMatchingItems)
{
    Variant first;
    first.SetByPath("name", Variant(std::string("a")));
    first.SetByPath("count", Variant(1));

    Variant second;
    second.SetByPath("name", Variant(std::string("b")));
    second.SetByPath("count", Variant(3));

    Variant root(VariantArray{ first, second });
    auto result = root.FilterByPathAndPredicate("count", [](const Variant& value) {
        return value.Is<int>() && value.To<int>() > 1;
    });

    ASSERT_EQ(1u, result.size());
    auto name = result[0].GetByPath("name");
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ("b", name->To<std::string>());
}

TEST(DataPropertyBagTest, PropertyBagGetsDefaultsAndNestedValues)
{
    PropertyBag bag;

    bag.Set("settings", "display.name", Variant(std::string("main")));

    EXPECT_EQ("main", bag.Get("settings", "display.name", Variant(std::string("fallback"))).To<std::string>());
    EXPECT_EQ("fallback", bag.Get("settings", "display.missing", Variant(std::string("fallback"))).To<std::string>());
    EXPECT_EQ(7, bag.Get("missing", Variant(7)).To<int>());
}

TEST(DataSerializerTest, RoundTripsStringWithEscapes)
{
    Variant source(std::string("a\"b\\c\nline"));

    Variant parsed = VariantSerializer::Deserialize(VariantSerializer::Serialize(source));

    ASSERT_TRUE(parsed.Is<std::string>());
    EXPECT_EQ(source.To<std::string>(), parsed.To<std::string>());
}

TEST(DataSerializerTest, RoundTripsObjectAndEscapedKey)
{
    ObjectMap object;
    object["plain"] = Variant(3);
    object["quote\"key"] = Variant(std::string("ok"));

    Variant parsed = VariantSerializer::Deserialize(VariantSerializer::Serialize(Variant(object)));

    ASSERT_TRUE(parsed.Is<ObjectMap>());
    const auto result = parsed.To<ObjectMap>();
    EXPECT_EQ(3, result.at("plain").To<int>());
    EXPECT_EQ("ok", result.at("quote\"key").To<std::string>());
}

TEST(DataSerializerTest, RoundTripsArrayTypes)
{
    Variant parsed = VariantSerializer::Deserialize(
        VariantSerializer::Serialize(Variant(Double3(1.25, 2.5, 3.75))));

    ASSERT_TRUE(parsed.Is<Double3>());
    const auto values = parsed.To<Double3>();
    EXPECT_DOUBLE_EQ(1.25, values[0]);
    EXPECT_DOUBLE_EQ(2.5, values[1]);
    EXPECT_DOUBLE_EQ(3.75, values[2]);
}

TEST(DataSerializerTest, RoundTripsCharAndByteAsNumericValues)
{
    Variant parsedChar = VariantSerializer::Deserialize(
        VariantSerializer::Serialize(Variant(static_cast<char>(65))));
    Variant parsedByte = VariantSerializer::Deserialize(
        VariantSerializer::Serialize(Variant(static_cast<uint8_t>(7))));

    ASSERT_TRUE(parsedChar.Is<char>());
    ASSERT_TRUE(parsedByte.Is<uint8_t>());
    EXPECT_EQ(static_cast<char>(65), parsedChar.To<char>());
    EXPECT_EQ(static_cast<uint8_t>(7), parsedByte.To<uint8_t>());
}

TEST(DataSerializerTest, InvalidUuidThrows)
{
    EXPECT_THROW(
        VariantSerializer::Deserialize("{\"__variant_type\":\"uuid\",\"value\":\"invalid\"}"),
        std::runtime_error);
}

TEST(DataUuidTest, GeneratedUuidCanRoundTripThroughString)
{
    auto id = GenerateNewUUID();
    auto parsed = uuid::from_string(to_string(id));

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(id, *parsed);
    EXPECT_FALSE(id.is_nil());
    EXPECT_TRUE(GenerateNilUUID().is_nil());
}

TEST(DataCommonFunctionTest, WrapHashAndEncodingHelpersWorkForSimpleCases)
{
    EXPECT_EQ(1, wrap(5, 0, 4));
    EXPECT_NE(HashCombine(1u, 2u), HashCombine(2u, 1u));
    EXPECT_EQ(FNV1a32("abc"), FNV1a32("abc"));

    const std::string ascii = "plain-ascii";
    EXPECT_EQ(ascii, UTF8ToLocal(LocalToUTF8(ascii)));
}

TEST(DataStableIdTest, StableIdUsesStable32BitParts)
{
    const auto scope = std::string("Renderer.Camera");
    const auto name = std::string("Main");

    const StableId32 scopeId = MakeStableId32(scope);
    const StableId32 nameId = MakeStableId32(name);
    const StableId id = MakeStableId(scope, name);

    EXPECT_EQ(FNV1a32(scope.c_str()), scopeId);
    EXPECT_EQ(FNV1a32(name.c_str()), nameId);
    EXPECT_EQ((uint64_t(scopeId) << 32) | nameId, id);
    EXPECT_EQ(id, MakeStableId(scope, name));
    EXPECT_NE(id, MakeStableId(scope, "Preview"));
}
