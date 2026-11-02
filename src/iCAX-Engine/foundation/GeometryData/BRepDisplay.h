#pragma once
#include "GeometryData.h"

namespace iCAX::GeometryData
{
    struct BRepDisplayEdge
    {
        std::uint64_t EdgeId = 0;
        bool Feature = true;
        std::vector<Point3> Points;
    };
    struct BRepDisplayEdges
    {
        std::vector<BRepDisplayEdge> Edges;
    };
}
