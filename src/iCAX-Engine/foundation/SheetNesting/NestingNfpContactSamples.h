#pragma once
#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace iCAX::SheetNesting::Detail
{
    struct NfpVertexPairDifferences
    {
        std::vector<Point> Values;
        std::vector<double> SortedX,SortedY;
        bool Complete=true;
    };

    // Preserve the original anchor-major, moving-minor prefix. These source
    // orientations do not depend on an NFP edge; only the following t formula
    // does. Store the same first subtraction, without normalizing coordinates
    // or changing the pair/sample limits, before reusing it on every edge.
    template<class Stop>
    NfpVertexPairDifferences PrepareNfpVertexPairDifferences(const std::vector<Point>& anchor,Pose anchorOrientation,
        const std::vector<Point>& moving,Pose movingOrientation,std::size_t maxPairs,Stop&& stop)
    {
        NfpVertexPairDifferences result;
        if(anchor.empty()||moving.empty()||!maxPairs)return result;
        std::vector<Point> transformedMoving;
        transformedMoving.reserve(std::min(moving.size(),maxPairs));
        for(std::size_t i=0;i<std::min(moving.size(),maxPairs);++i)
        {
            if(stop()){result.Complete=false;return result;}
            transformedMoving.push_back(Transform(moving[i],movingOrientation));
        }
        result.Values.reserve(maxPairs);
        for(const auto source:anchor)
        {
            if(result.Values.size()>=maxPairs)break;
            if(stop()){result.Complete=false;return result;}
            const auto fixed=Transform(source,anchorOrientation);
            for(const auto next:transformedMoving)
            {
                if(result.Values.size()>=maxPairs)break;
                if(stop()){result.Complete=false;return result;}
                result.Values.push_back({fixed.X-next.X,fixed.Y-next.Y});
            }
        }
        for(const auto difference:result.Values)
        {
            if(stop()){result.Complete=false;return result;}
            if(std::isfinite(difference.X))result.SortedX.push_back(difference.X);
            if(std::isfinite(difference.Y))result.SortedY.push_back(difference.Y);
        }
        for(auto* axis:{&result.SortedX,&result.SortedY})
        {
            std::sort(axis->begin(),axis->end());
            axis->erase(std::unique(axis->begin(),axis->end()),axis->end());
        }
        return result;
    }

    template<class Stop>
    bool AppendNfpVertexPairCuts(const NfpVertexPairDifferences& differences,Point a,Point b,double tolerance,
        std::vector<double>& cuts,Stop&& stop)
    {
        const double dx=b.X-a.X,dy=b.Y-a.Y;
        const bool useX=std::abs(dx)>tolerance,useY=std::abs(dy)>tolerance;
        for(const auto difference:differences.Values)
        {
            if(stop())return false;
            if(useX){const auto t=(difference.X-a.X)/dx;if(t>0&&t<1)cuts.push_back(t);}
            if(useY){const auto t=(difference.Y-a.Y)/dy;if(t>0&&t<1)cuts.push_back(t);}
        }
        return true;
    }

    template<class Stop>
    bool AppendNfpVertexPairIntervalCuts(const NfpVertexPairDifferences& differences,Point a,Point b,double tolerance,
        std::vector<double>& cuts,Stop&& stop)
    {
        if(stop())return false;
        const double dx=b.X-a.X,dy=b.Y-a.Y;
        if(!std::isfinite(a.X)||!std::isfinite(a.Y)||!std::isfinite(b.X)||!std::isfinite(b.Y)||
            !std::isfinite(dx)||!std::isfinite(dy))
            return AppendNfpVertexPairCuts(differences,a,b,tolerance,cuts,stop);
        const auto append=[&](const std::vector<double>& axis,double start,double end,double span)
        {
            if(!(std::abs(span)>tolerance))return true;
            // Floating subtraction is monotone. Outside this strict interval
            // (d-start)/span cannot lie in (0,1), including reversed edges.
            // The range is necessary only: retain the original formula and
            // open-interval test for each selected, exact stored difference.
            const auto first=std::upper_bound(axis.begin(),axis.end(),std::min(start,end));
            const auto last=std::lower_bound(first,axis.end(),std::max(start,end));
            for(auto value=first;value!=last;++value)
            {
                if(stop())return false;
                const auto t=(*value-start)/span;if(t>0&&t<1)cuts.push_back(t);
            }
            return true;
        };
        // Exact duplicate differences yield duplicate t values removed by the
        // caller's established final sort/unique. Raw insertion order is not
        // preserved; the final alignment cuts and original gates are.
        return append(differences.SortedX,a.X,b.X,dx)&&append(differences.SortedY,a.Y,b.Y,dy);
    }
}
