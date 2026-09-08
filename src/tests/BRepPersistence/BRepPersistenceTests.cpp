#include "GeometryData/BRepPersistence.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
using namespace iCAX::GeometryData;
using namespace iCAX::GeometryData::Persistence;
void Expect(bool value) { if (!value) throw std::runtime_error("Test failed"); }
template<class F> void Reject(F&& f) { bool rejected = false; try { f(); } catch (const std::runtime_error&) { rejected = true; } Expect(rejected); }

// Exercise every explicitly serialized member, including non-default metadata,
// large IDs, transforms, orientations, knot vectors and nested collections.
struct Fill {
    std::uint64_t seed = 9007199254740993ULL;
    template<class T> requires std::is_integral_v<T>
    void Set(T& value) { value = static_cast<T>(++seed); }
    void Set(bool& value) { value = (++seed & 1) != 0; }
    void Set(double& value) { value = static_cast<double>(++seed % 101) / 7.0; }
    template<class T> requires std::is_enum_v<T>
    void Set(T& value) { value = static_cast<T>(++seed % 4); }
    void Set(std::string& value) { value = "曲面/metadata-" + std::to_string(++seed); }
    template<class T> void Set(std::vector<T>& value) { value.resize(2); for (auto& item : value) Set(item); }
    template<class T, std::size_t N> void Set(std::array<T,N>& value) { for (auto& item : value) Set(item); }
    template<class... T> void Set(std::variant<T...>& value) { std::visit([&](auto& item) { Set(item); }, value); }
    template<class T> requires requires { Members(static_cast<T*>(nullptr)); }
    void Set(T& value) { std::apply([&](auto... member) { (Set(value.*member), ...); }, Members(static_cast<T*>(nullptr))); }
};
template<std::size_t I = 0, class Record> void AllVariants(std::vector<Record>& records, Fill& fill) {
    using V = decltype(Record{}.Geometry);
    if constexpr (I < std::variant_size_v<V>) {
        Record record; record.Geometry.template emplace<I>(); fill.Set(record);
        records.push_back(std::move(record)); AllVariants<I+1>(records, fill);
    }
}
int main() {
    try {
        BRepModel model; Fill fill; fill.Set(model);
        AllVariants(model.Curves2, fill); AllVariants(model.Curves3, fill); AllVariants(model.Surfaces3, fill);
        const auto bytes = Serialize(model);
        const auto restored = Deserialize(bytes);
        Expect(Serialize(restored) == bytes);
        Expect(restored.Metadata.Name == model.Metadata.Name);
        Expect(restored.Faces[0].Id == model.Faces[0].Id);
        Expect(restored.RootShapes[0].Location.Matrix.Values == model.RootShapes[0].Location.Matrix.Values);
        for (std::size_t n = 0; n < bytes.size(); ++n) Reject([&] { Deserialize(std::span(bytes).first(n)); });
        auto invalid = bytes; invalid[0] ^= 1; Reject([&] { Deserialize(invalid); });
        invalid = bytes; invalid[7] = '2'; Reject([&] { Deserialize(invalid); });
        invalid = bytes; invalid.push_back(0); Reject([&] { Deserialize(invalid); });
        invalid = bytes; for (int n = 8; n < 12; ++n) invalid[n] = 255; Reject([&] { Deserialize(invalid); });
        { const std::array<std::uint8_t,4> bad{255,255,255,255}; Reader r(bad); Curve3 v; Reject([&] { r.Read(v); }); }
        { const std::array<std::uint8_t,1> bad{255}; Reader r(bad); bool v; Reject([&] { r.Read(v); }); }
        { const std::array<std::uint8_t,1> bad{255}; Reader r(bad); ETopologyOrientation v; Reject([&] { r.Read(v); }); }
        // A real file is closed, reopened and removed; no user files are touched.
        const auto dir = std::filesystem::temp_directory_path() / ("brep-roundtrip-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Expect(std::filesystem::create_directory(dir));
        const auto path = dir / "test.brep-payload";
        { std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); Expect(out.good()); }
        { std::ifstream in(path, std::ios::binary); std::vector<std::uint8_t> disk((std::istreambuf_iterator<char>(in)), {}); Expect(Serialize(Deserialize(disk)) == bytes); }
        std::filesystem::remove(path); std::filesystem::remove(dir);
        std::cout << "BRep roundtrip: all members, all curve/surface alternatives, disk reopen and " << bytes.size() << " truncations passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
