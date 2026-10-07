#include "pch.h"


#include <Data/Array1.h>
#include <Data/Array2.h>
#include <Data/CommonFunction.h>
#include <Data/PropertyBag.h>
#include <Data/StableId.h>
#include <Data/VariantSerializer.h>
#include <Data/uuid.h>


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

TEST(DataVariantTest, CompletedNestedDocumentsTransferStorageAndCopiesRemainIndependent)
{
    VariantArray rows{ObjectMap{{"parameters", ObjectMap{{"clearance", 0.1}}}}};
    const auto* rowStorage = rows.data();
    Variant movedRows(std::move(rows));
    EXPECT_EQ(std::get<VariantArray>(movedRows.m_Value).data(), rowStorage);
    ObjectMap document{{"stocks", std::move(movedRows)}};
    const auto* fieldStorage = &document.at("stocks");
    Variant movedDocument(std::move(document));
    EXPECT_EQ(&std::get<ObjectMap>(movedDocument.m_Value).at("stocks"), fieldStorage);

    Variant copiedDocument = movedDocument;
    auto& originalRows = std::get<VariantArray>(std::get<ObjectMap>(movedDocument.m_Value).at("stocks").m_Value);
    auto& originalParameters = std::get<ObjectMap>(std::get<ObjectMap>(originalRows.front().m_Value).at("parameters").m_Value);
    originalParameters["clearance"] = 0.2;
    const auto copiedRows = copiedDocument.To<ObjectMap>().at("stocks").To<VariantArray>();
    EXPECT_EQ(copiedRows.front().To<ObjectMap>().at("parameters").To<ObjectMap>().at("clearance").To<double>(), 0.1);
    EXPECT_EQ(VariantSerializer::Deserialize(VariantSerializer::Serialize(copiedDocument)), copiedDocument);
    const auto* transferredField = &std::get<ObjectMap>(movedDocument.m_Value).at("stocks");
    const auto transferredDocument = std::move(movedDocument).To<ObjectMap>();
    EXPECT_EQ(&transferredDocument.at("stocks"), transferredField);
}

TEST(DataVariantTest, NestedValueCopyAssignmentOwnsIndependentStorage)
{
    Variant original(ObjectMap{{"rows", VariantArray{
        ObjectMap{{"parameters", ObjectMap{{"clearance", 0.1}}}}
    }}});
    Variant constructed(original);
    Variant assigned(42);
    assigned = original;
    assigned = assigned;

    const auto& originalFields = std::get<ObjectMap>(original.m_Value);
    const auto& constructedFields = std::get<ObjectMap>(constructed.m_Value);
    const auto& assignedFields = std::get<ObjectMap>(assigned.m_Value);
    EXPECT_NE(&originalFields.at("rows"), &constructedFields.at("rows"));
    EXPECT_NE(&originalFields.at("rows"), &assignedFields.at("rows"));
    const auto& originalRows = std::get<VariantArray>(originalFields.at("rows").m_Value);
    const auto& constructedRows = std::get<VariantArray>(constructedFields.at("rows").m_Value);
    const auto& assignedRows = std::get<VariantArray>(assignedFields.at("rows").m_Value);
    EXPECT_NE(originalRows.data(), constructedRows.data());
    EXPECT_NE(originalRows.data(), assignedRows.data());

    original.SetByPath("rows[0].parameters.clearance", 0.2);
    constructed.SetByPath("rows[0].parameters.clearance", 0.3);
    EXPECT_EQ(original.GetByPath("rows[0].parameters.clearance")->To<double>(), 0.2);
    EXPECT_EQ(constructed.GetByPath("rows[0].parameters.clearance")->To<double>(), 0.3);
    EXPECT_EQ(assigned.GetByPath("rows[0].parameters.clearance")->To<double>(), 0.1);
}

TEST(DataVariantTest, MoveSpecialMembersTransferStorageAndPreserveUnderlyingExceptionTraits)
{
    static_assert(std::is_nothrow_move_constructible_v<Variant>
        == std::is_nothrow_move_constructible_v<Variant::VariantType>);
    static_assert(std::is_nothrow_move_assignable_v<Variant>
        == std::is_nothrow_move_assignable_v<Variant::VariantType>);
    static_assert(std::is_nothrow_destructible_v<Variant>
        == std::is_nothrow_destructible_v<Variant::VariantType>);

    Variant source(ObjectMap{{"rows", VariantArray{ObjectMap{{"value", 7}}}}});
    const auto* field = &std::get<ObjectMap>(source.m_Value).at("rows");
    const auto* rows = std::get<VariantArray>(field->m_Value).data();
    Variant constructed(std::move(source));
    EXPECT_EQ(&std::get<ObjectMap>(constructed.m_Value).at("rows"), field);
    EXPECT_EQ(std::get<VariantArray>(field->m_Value).data(), rows);
    source = 8;
    EXPECT_EQ(source.To<int>(), 8);

    Variant assigned(std::string("old value"));
    assigned = std::move(constructed);
    EXPECT_EQ(&std::get<ObjectMap>(assigned.m_Value).at("rows"), field);
    EXPECT_EQ(std::get<VariantArray>(field->m_Value).data(), rows);
    EXPECT_EQ(assigned.GetByPath("rows[0].value")->To<int>(), 7);
    constructed = VariantArray{1, 2};
    EXPECT_EQ(constructed.To<VariantArray>().size(), 2u);

    Variant arraySource(VariantArray{ObjectMap{{"name", std::string("part")}}});
    const auto* arrayStorage = std::get<VariantArray>(arraySource.m_Value).data();
    Variant arrayDestination(3.0);
    arrayDestination = std::move(arraySource);
    EXPECT_EQ(std::get<VariantArray>(arrayDestination.m_Value).data(), arrayStorage);
    EXPECT_EQ(arrayDestination.GetByPath("[0].name")->To<std::string>(), "part");
}

TEST(DataVariantTest, SpecialMemberCopiesAndAssignmentsRetainExactSimpleTypes)
{
    VariantArray values{Variant(), true, static_cast<char>(65), static_cast<uint8_t>(7),
        42, 1.25, std::string("text"), GenerateNewUUID(), Double3(1.25, 2.5, 3.75),
        Int2x2(std::vector<int>{1, 2, 3, 4})};
    for (const auto& value : values)
    {
        Variant copied(value);
        Variant assigned(ObjectMap{{"old", 1}});
        assigned = value;
        EXPECT_EQ(copied.m_Value.index(), value.m_Value.index());
        EXPECT_EQ(assigned.m_Value.index(), value.m_Value.index());
        EXPECT_EQ(copied, value);
        EXPECT_EQ(assigned, value);
        Variant moved(std::move(copied));
        assigned = std::move(moved);
        EXPECT_EQ(assigned.m_Value.index(), value.m_Value.index());
        EXPECT_EQ(assigned, value);
    }
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

TEST(DataSerializerTest, LargeStringSpansKeepAllBytesAndRejectTruncatedEscapes)
{
    std::string text(256*1024,'p');
    text.append("\"\\\b\f\n\r\t中文");
    text.push_back('\0');
    text.append(256*1024,'q');
    const ObjectMap source{{"escaped\"key\\中文",text},{"empty",std::string{}}};
    const auto restored=VariantSerializer::Deserialize(VariantSerializer::Serialize(Variant(source)));
    EXPECT_EQ(restored.To<ObjectMap>(),source);
    EXPECT_THROW(VariantSerializer::Deserialize(
        "{\"__variant_type\":\"string\",\"value\":\"unfinished"),std::runtime_error);
    EXPECT_THROW(VariantSerializer::Deserialize(
        "{\"__variant_type\":\"string\",\"value\":\"unfinished\\"),std::runtime_error);
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
