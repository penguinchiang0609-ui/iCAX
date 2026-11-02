#include "GeometryData/TubeNeutralPersistence.h"
#include <iostream>
using namespace iCAX::GeometryData;
using namespace iCAX::GeometryData::Tube;
using iCAX::GeometryData::Persistence::Members;
void Expect(bool value) { if(!value)throw std::runtime_error("Tube CAD persistence test failed"); }
template<class F> void Reject(F&& action) { bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}Expect(rejected); }
struct Fill {
    std::uint64_t seed=9007199254740993ull;
    template<class T> requires std::is_integral_v<T> void Set(T& value) { value=static_cast<T>(++seed); }
    void Set(bool& value) { value=true; }
    void Set(double& value) { value=static_cast<double>(++seed%101)/7.0; }
    template<class T> requires std::is_enum_v<T> void Set(T& value) { value=static_cast<T>(0); }
    void Set(std::string& value) { value="精确曲线/feature-"+std::to_string(++seed); }
    template<class T> void Set(std::optional<T>& value) { value.emplace();Set(*value); }
    template<class T> void Set(std::vector<T>& value) { value.resize(2);for(auto& item:value)Set(item); }
    template<class K,class V> void Set(std::map<K,V>& value) { for(int i=0;i<2;++i){K key;V item;Set(key);Set(item);value.emplace(std::move(key),std::move(item));} }
    template<class T,std::size_t N> void Set(std::array<T,N>& value) { for(auto& item:value)Set(item); }
    template<class... T> void Set(std::variant<T...>& value) {std::visit([&](auto& item){Set(item);},value);}
    template<class T> requires requires { Members(static_cast<T*>(nullptr)); }
    void Set(T& value) {std::apply([&](auto... member){(Set(value.*member),...);},Members(static_cast<T*>(nullptr)));}
};
template<std::size_t I=0> void AddNodes(CTubeNeutralGeometry& model,Fill& fill) {
    if constexpr(I<std::variant_size_v<SolidNodeData>) {SSolidNode node;node.Data.emplace<I>();fill.Set(node);model.SolidNodes.push_back(std::move(node));AddNodes<I+1>(model,fill);}
}
template<std::size_t I=0> void AddCurves(CPlanarRegion2& region,Fill& fill) {
    using Curve=decltype(CurveSegment2{}.Curve);
    if constexpr(I<std::variant_size_v<Curve>) {SRegionCurve2 curve;curve.Segment.Curve.emplace<I>();fill.Set(curve);region.Boundaries.front().Curves.push_back(std::move(curve));AddCurves<I+1>(region,fill);}
}
int main() {
    try {
        CTubeCadDocument document; Fill fill;fill.Set(document);AddNodes(document.Construction,fill);
        auto& extrusion=std::get<SExtrudedRegionNode>(document.Construction.SolidNodes.front().Data);AddCurves(extrusion.Section,fill);
        SUVFeature image;image.Data.emplace<SUVImage>();fill.Set(image);document.Construction.UVFeatures.push_back(image);
        auto& wrapped=std::get<SWrappedVolumeNode>(document.Construction.SolidNodes[7].Data);
        wrapped.LowerNormalOffset.emplace<SScalarGrid2>();fill.Set(wrapped.LowerNormalOffset);
        const auto bytes=Tube::Persistence::Serialize(document);
        const auto restored=Tube::Persistence::DeserializeDocument(bytes);
        Expect(Tube::Persistence::Serialize(restored)==bytes);
        Expect(restored.Revision==document.Revision&&restored.Revision>9007199254740992ull);
        Expect(restored.Construction.SolidNodes.size()==document.Construction.SolidNodes.size());
        for(std::size_t n=0;n<bytes.size();n+=std::max(std::size_t(1),bytes.size()/127))
            Reject([&]{Tube::Persistence::DeserializeDocument(std::span(bytes).first(n));});
        auto bad=bytes;bad.push_back(0);Reject([&]{Tube::Persistence::DeserializeDocument(bad);});
        bad=bytes;bad[7]='1';Reject([&]{Tube::Persistence::DeserializeDocument(bad);});
        const auto graph=Tube::Persistence::Serialize(document.Construction);
        Expect(Tube::Persistence::Serialize(Tube::Persistence::DeserializeGeometry(graph))==graph);
        const auto boundary=Tube::Persistence::Serialize(document.Geometry);
        Expect(Tube::Persistence::Serialize(Tube::Persistence::DeserializeBoundary(boundary))==boundary);
        Reject([&]{Tube::Persistence::DeserializeGeometry(boundary);});
        Reject([&]{Tube::Persistence::DeserializeBoundary(bytes);});
        Reject([&]{Tube::Persistence::DeserializeBoundary(std::span(boundary).first(boundary.size()-1));});
        Reject([&]{Tube::Persistence::DeserializeGeometry(bytes);});
        std::cout<<"PASS: all 10 solid variants, both UV variants, every curve, nested relations, metadata, embedded bodies, 64-bit identities, malformed payloads\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
