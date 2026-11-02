#pragma once

#include "OpenCascadeTaskExecution.h"
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <stdexcept>
#include <vector>

namespace iCAX::OpenCascade
{
    // Same exact bounding operation as AddOptimal(shape, ..., false, false).
    // Include free edges/vertices as well as faces. Workers own deep copies:
    // OCCT surface/curve evaluation can populate caches even during a query.
    inline Bnd_Box ExactShapeBounds(const TopoDS_Shape& shape,
        std::size_t maximumConcurrency = 0)
    {
        std::vector<TopoDS_Shape> pieces;
        for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
            pieces.push_back(it.Current());
        for (TopExp_Explorer it(shape, TopAbs_EDGE, TopAbs_FACE); it.More(); it.Next())
            pieces.push_back(it.Current());
        for (TopExp_Explorer it(shape, TopAbs_VERTEX, TopAbs_EDGE); it.More(); it.Next())
            pieces.push_back(it.Current());
        Bnd_Box bounds;
        // Cloning hundreds of small supports can cost more than their bounds.
        // Reserve the pool for large models; smaller inputs keep the direct query.
        if (pieces.size() < 512 || detail::GeometryConcurrency(maximumConcurrency) == 1)
        {
            BRepBndLib::AddOptimal(shape, bounds, false, false);
            return bounds;
        }
        for (auto& piece : pieces)
        {
            COpenCascadeCancellationScope::ThrowIfCancellationRequested();
            BRepBuilderAPI_Copy copy(piece, true, false);
            if (!copy.IsDone() || copy.Shape().IsNull())
                throw std::runtime_error("Exact bounds could not isolate geometry");
            piece = copy.Shape();
        }
        std::vector<Bnd_Box> partial(pieces.size());
        detail::ForGeometryIndices(pieces.size(), maximumConcurrency, [&](std::size_t i) {
            BRepBndLib::AddOptimal(pieces[i], partial[i], false, false);
        });
        for (const auto& box : partial) bounds.Add(box);
        return bounds;
    }
}
