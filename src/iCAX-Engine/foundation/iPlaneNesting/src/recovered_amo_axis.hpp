#pragma once
#include "recovered_amo_map.hpp"
#include <optional>
namespace study::recovered {
struct AmoAxisSegment {Point from{},to{};double tolerance=1e-6;bool operator==(const AmoAxisSegment &other)const{return from.x==other.from.x&&from.y==other.from.y&&to.x==other.to.x&&to.y==other.to.y&&tolerance==other.tolerance;}};
static_assert(sizeof(AmoAxisSegment)==40);
struct AmoAxisNode {
    AmoAxisSegment segment;double minimum=0,maximum=0;int32_t height=1,live=1,count=1,endpoint=0;bool deleted=false;
    std::shared_ptr<AmoAxisNode> left,right;
};
struct AmoTilingAxisIndex {
    std::shared_ptr<AmoAxisNode> root;Point direction{};
    std::array<double,7> forward{},inverse{};double tolerance=1e-6;
};
struct AmoAxisQueryState {double coordinate=0,reference=0;bool unrestricted=true,firstEmpty=true;double first=0;bool secondEmpty=true;double second=0;};
void amoAxisConsiderSegment(AmoAxisQueryState &,const AmoAxisSegment &); //8c2350
std::shared_ptr<AmoAxisNode> amoAxisBuildTree(std::vector<AmoAxisSegment> &,size_t first,size_t last,int32_t endpoint=0); //8c2aa0
AmoTilingAxisIndex amoBuildAxisIndex(const AmoTilingContours &,Point); //8c2570
std::optional<Point> amoQueryAxisIndex(const AmoTilingAxisIndex &,Point); //8c28f0
}
