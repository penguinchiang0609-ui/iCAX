#pragma once
#include "GeometryFeatureAnalysisService.h"
#include <algorithm>
#include <cmath>

namespace iCAX::Geometry::Detail
{
    // A CAD vertex represents a tolerance ball. Adjacent exact curves may
    // terminate at different points inside that same ball without an open BRep.
    inline bool SupportsBoundaryJunction(const GeometryData::BRepModel& model,
        const SBoundaryCurveSpan& a,const SBoundaryCurveSpan& b,
        const GeometryData::Point3& end,const GeometryData::Point3& start,double joinTolerance)
    {
        const auto distance=[](const auto& p,const auto& q){return std::hypot(p.X-q.X,p.Y-q.Y,p.Z-q.Z);};
        if(distance(end,start)<=joinTolerance)return true;
        const GeometryData::BRepEdge *ea=nullptr,*eb=nullptr;
        for(const auto& edge:model.Edges){if(edge.Id==a.EdgeID)ea=&edge;if(edge.Id==b.EdgeID)eb=&edge;}
        if(!ea||!eb)return false;
        for(const auto id:{ea->StartVertexId,ea->EndVertexId}){
            if(!id||(id!=eb->StartVertexId&&id!=eb->EndVertexId))continue;
            for(const auto& vertex:model.Vertices)if(vertex.Id==id&&std::isfinite(vertex.Tolerance)&&vertex.Tolerance>=0){
                const auto tolerance=vertex.Tolerance+1e-9;
                if(distance(end,vertex.Position)<=tolerance&&distance(start,vertex.Position)<=tolerance)return true;
            }
        }
        return false;
    }
}
