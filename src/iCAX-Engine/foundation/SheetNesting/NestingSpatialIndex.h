#pragma once
#include "SheetNesting.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace iCAX::SheetNesting::Detail
{
    struct SpatialBounds
    {
        double Left=0,Bottom=0,Right=0,Top=0;
        static SpatialBounds From(const Rect& r)
        { return {r.X,r.Y,r.X+r.Width,r.Y+r.Height}; }
        bool Finite() const
        { return std::isfinite(Left)&&std::isfinite(Bottom)&&std::isfinite(Right)&&std::isfinite(Top)&&Left<=Right&&Bottom<=Top; }
    };

    // Immutable, job-local broad phase only. It never certifies geometry or changes
    // tolerance. Closed AABB queries include every possible narrow-phase conflict.
    class SpatialIndex
    {
    public:
        explicit SpatialIndex(std::vector<SpatialBounds> bounds):Boxes(std::move(bounds))
        {
            Order.reserve(Boxes.size());
            for(std::size_t i=0;i<Boxes.size();++i)
                (Boxes[i].Finite()?Order:Unindexed).push_back(i);
            Nodes.reserve(Order.size()*2);
            if(!Order.empty())Build(0,Order.size());
        }
        std::vector<std::size_t> Query(SpatialBounds box,double gap=0,
            std::size_t exclusiveUpper=std::numeric_limits<std::size_t>::max()) const
        {
            std::vector<std::size_t> found;
            exclusiveUpper=std::min(exclusiveUpper,Boxes.size());
            if(!box.Finite()||!std::isfinite(gap)||gap<0)
            {
                found.resize(exclusiveUpper);std::iota(found.begin(),found.end(),0);return found;
            }
            // Overflow expands to infinity and therefore includes more objects.
            // Do not round a gap down or subtract the geometric tolerance here.
            const auto infinity=std::numeric_limits<double>::infinity();
            box.Left=std::nextafter(box.Left-gap,-infinity);box.Right=std::nextafter(box.Right+gap,infinity);
            box.Bottom=std::nextafter(box.Bottom-gap,-infinity);box.Top=std::nextafter(box.Top+gap,infinity);
            for(auto i:Unindexed)if(i<exclusiveUpper)found.push_back(i);
            if(!Nodes.empty())Visit(0,box,exclusiveUpper,found);
            std::sort(found.begin(),found.end());
            return found;
        }
    private:
        struct Node {SpatialBounds Box;std::size_t Begin=0,End=0,Left=0,Right=0;};
        static bool Intersects(const SpatialBounds& a,const SpatialBounds& b)
        { return !(a.Right<b.Left||b.Right<a.Left||a.Top<b.Bottom||b.Top<a.Bottom); }
        std::size_t Build(std::size_t begin,std::size_t end)
        {
            auto box=Boxes[Order[begin]];
            for(auto i=begin+1;i<end;++i)
            {
                const auto& b=Boxes[Order[i]];
                box.Left=std::min(box.Left,b.Left);box.Bottom=std::min(box.Bottom,b.Bottom);
                box.Right=std::max(box.Right,b.Right);box.Top=std::max(box.Top,b.Top);
            }
            const auto index=Nodes.size();Nodes.push_back({box,begin,end});
            if(end-begin<=8)return index;
            const bool x=box.Right-box.Left>=box.Top-box.Bottom;
            const auto middle=begin+(end-begin)/2;
            std::nth_element(Order.begin()+static_cast<std::ptrdiff_t>(begin),
                Order.begin()+static_cast<std::ptrdiff_t>(middle),Order.begin()+static_cast<std::ptrdiff_t>(end),
                [&](auto a,auto b)
                {
                    const auto ca=x?std::midpoint(Boxes[a].Left,Boxes[a].Right):std::midpoint(Boxes[a].Bottom,Boxes[a].Top);
                    const auto cb=x?std::midpoint(Boxes[b].Left,Boxes[b].Right):std::midpoint(Boxes[b].Bottom,Boxes[b].Top);
                    return ca!=cb?ca<cb:a<b;
                });
            const auto left=Build(begin,middle),right=Build(middle,end);
            Nodes[index].Left=left;Nodes[index].Right=right;return index;
        }
        void Visit(std::size_t index,const SpatialBounds& box,std::size_t upper,std::vector<std::size_t>& found) const
        {
            const auto& node=Nodes[index];if(!Intersects(box,node.Box))return;
            if(node.End-node.Begin<=8)
            {
                for(auto j=node.Begin;j<node.End;++j)
                {const auto i=Order[j];if(i<upper&&Intersects(box,Boxes[i]))found.push_back(i);}
                return;
            }
            Visit(node.Left,box,upper,found);Visit(node.Right,box,upper,found);
        }
        std::vector<SpatialBounds> Boxes;
        std::vector<std::size_t> Order,Unindexed;
        std::vector<Node> Nodes;
    };
}
