#include "recovered_fuzzy_polygon_iterator.hpp"
namespace study::recovered {
bool FuzzyPathIterator::empty()const{return stack_.empty();}
ConvolutionIntegerSegment FuzzyPathIterator::back()const{
    require(!stack_.empty(),PN2S_INVALID,"No last fuzzy path segment");const auto &entry=stack_.back();
    if(entry.fresh){require(!entry.fresh->empty(),PN2S_INVALID,"Empty fuzzy raw path");return entry.fresh->back();}
    require(entry.existing&&entry.end>0&&size_t(entry.end)<=entry.existing->size(),PN2S_INVALID,"Invalid fuzzy existing path end");return entry.existing->at(size_t(entry.end-1));
}
FuzzyGeometryRange FuzzyPathIterator::view(size_t index){
    auto &entry=stack_.at(index);
    if(!entry.fresh){require(entry.existing&&entry.end>0&&size_t(entry.end)<=entry.existing->size(),PN2S_INVALID,"Invalid fuzzy range finalization");ends_.erase(entry.existing->at(size_t(entry.end-1)));return {entry.existing,entry.begin,entry.end};}
    require(!entry.fresh->empty(),PN2S_INVALID,"Visiting an empty fuzzy raw path");
    // 83fee0 publishes a distinct immutable path; the original fresh vector
    // can subsequently be shortened without changing the emitted region.
    auto result=state_.registerPath(*entry.fresh);
    for(const auto &segment:*entry.fresh)points_.erase(segment[0]);points_.erase(entry.fresh->back()[1]);return result;
}
void FuzzyPathIterator::restoreFreshEnd(){
    if(stack_.empty()||!stack_.back().fresh)return;
    const auto &path=*stack_.back().fresh;
    if(path.empty())return;
    require(path.size()<=size_t(INT32_MAX),PN2S_INVALID,"Fuzzy raw path index exceeds its native range");points_[path.back()[1]]={stack_.size()-1,int32_t(path.size())};
}
std::optional<FuzzyRegionGeometry> FuzzyPathIterator::append(const ConvolutionIntegerSegment &segment){
    require(empty()||back()[1]==segment[0],PN2S_INVALID,"Consecutive fuzzy segments must share their endpoint");
    const bool started=empty()||!stack_.back().fresh;
    if(started)stack_.push_back({std::make_shared<std::vector<ConvolutionIntegerSegment>>(),{},-1,-1});
    const auto hit=points_.find(segment[1]);
    if(hit==points_.end()){
        auto &path=*stack_.back().fresh;path.push_back(segment);require(path.size()<=size_t(INT32_MAX),PN2S_INVALID,"Fuzzy raw path index exceeds its native range");points_[segment[1]]={stack_.size()-1,int32_t(path.size())};
        if(started)points_[segment[0]]={stack_.size()-1,0};return {};
    }
    const auto [index,position]=hit->second;require(index<stack_.size()&&stack_[index].fresh,PN2S_INVALID,"Raw fuzzy reentry requires its fresh path");
    const auto reentered=stack_[index].fresh;require(position>=0&&size_t(position)<=reentered->size(),PN2S_INVALID,"Raw fuzzy reentry is outside its path");
    FuzzyRegionGeometry result;
    // Preserve the native range stack order. The closing raw path is emitted
    // after the intervening ranges, with the reentered tail appended to it.
    for(size_t n=index+1;n+1<stack_.size();++n)result.push_back(view(n));
    if(index+1==stack_.size()){
        auto tail=std::make_shared<std::vector<ConvolutionIntegerSegment>>(reentered->begin()+position,reentered->end());tail->push_back(segment);stack_.push_back({std::move(tail),{},-1,-1});
    }else{
        auto &tail=*stack_.back().fresh;tail.push_back(segment);tail.insert(tail.end(),reentered->begin()+position,reentered->end());
    }
    result.push_back(view(stack_.size()-1));reentered->resize(size_t(position));stack_.resize(index+1);
    if(reentered->empty())stack_.pop_back();restoreFreshEnd();return result;
}
std::optional<FuzzyRegionGeometry> FuzzyPathIterator::appendRange(
    std::shared_ptr<const std::vector<ConvolutionIntegerSegment>> path,int32_t begin,int32_t end){
    require(path&&begin>=0&&begin<end&&size_t(end)<=path->size(),PN2S_INVALID,"Invalid fuzzy existing range");
    require(empty()||back()[1]==path->at(size_t(begin))[0],PN2S_INVALID,"Consecutive fuzzy paths must share their endpoint");
    const auto last=path->at(size_t(end-1));const auto ended=ends_.find(last);
    if(ended==ends_.end()){
        stack_.push_back({{},path,begin,end});ends_[last]=stack_.size()-1;
        const auto hit=points_.find(last[1]);if(hit==points_.end())return {};
        const auto [index,position]=hit->second;require(index<stack_.size()&&stack_[index].fresh,PN2S_INVALID,"Fuzzy point reentry requires its fresh path");const auto reentered=stack_[index].fresh;
        require(position>=0&&size_t(position)<=reentered->size(),PN2S_INVALID,"Fuzzy point reentry is outside its raw path");
        if(size_t(position)<reentered->size()){
            auto tail=std::make_shared<std::vector<ConvolutionIntegerSegment>>(reentered->begin()+position,reentered->end());stack_.push_back({std::move(tail),{},-1,-1});
        }
        FuzzyRegionGeometry result;for(size_t n=index+1;n<stack_.size();++n)result.push_back(view(n));
        reentered->resize(size_t(position));stack_.resize(index+1);if(reentered->empty())stack_.pop_back();restoreFreshEnd();return result;
    }
    const auto index=ended->second;require(index<stack_.size()&&!stack_[index].fresh,PN2S_INVALID,"Fuzzy range reentry requires its existing path");
    FuzzyRegionGeometry result;for(size_t n=index+1;n<stack_.size();++n)result.push_back(view(n));
    stack_.push_back({{},std::move(path),begin,end});result.push_back(view(stack_.size()-1));stack_.resize(index+1);
    auto &reentered=stack_.back();
    if(reentered.begin<begin){reentered.end=begin;require(size_t(begin)<=reentered.existing->size(),PN2S_INVALID,"Fuzzy range reentry prefix is outside its source");ends_[reentered.existing->at(size_t(begin-1))]=index;}
    else stack_.pop_back();
    return result;
}
}
