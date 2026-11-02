#pragma once

#include "GeometryData/GeometryData.h"
#include <unordered_set>

namespace iCAX::ExtrusionRecognition::detail
{
    // A classification partition is a view of one converted split model.
    // Preserve exact geometry records and IDs; retain only referenced topology.
    inline GeometryData::BRepModel SectionFaceSubset(const GeometryData::BRepModel& source,
        const std::vector<std::uint64_t>& faceIDs)
    {
        using IDs=std::unordered_set<std::uint64_t>;
        const IDs faces(faceIDs.begin(),faceIDs.end());
        IDs wires,edges,vertices,curves2,curves3,surfaces,meshes;
        GeometryData::BRepModel result;result.Metadata=source.Metadata;
        for(const auto& face:source.Faces)if(faces.contains(face.Id)){
            result.Faces.push_back(face);surfaces.insert(face.Surface3Id);meshes.insert(face.Triangulation3Id);
            wires.insert(face.WireIds.begin(),face.WireIds.end());
            result.RootFaceIds.push_back(face.Id);
            result.RootShapes.push_back({GeometryData::EBRepShapeKind::Face,face.Id,face.Orientation,{}});
        }
        for(const auto& wire:source.Wires)if(wires.contains(wire.Id)){
            result.Wires.push_back(wire);
            for(const auto& coedge:wire.Coedges){
                edges.insert(coedge.EdgeId);curves2.insert(coedge.Curve2Id);
                vertices.insert(coedge.StartVertexId);vertices.insert(coedge.EndVertexId);
            }
        }
        for(const auto& edge:source.Edges)if(edges.contains(edge.Id)){
            result.Edges.push_back(edge);curves3.insert(edge.Curve3Id);
            vertices.insert(edge.StartVertexId);vertices.insert(edge.EndVertexId);
        }
        const auto copy=[](const auto& records,const IDs& selected,auto& target){
            for(const auto& item:records)if(selected.contains(item.Id))target.push_back(item);
        };
        copy(source.Vertices,vertices,result.Vertices);copy(source.Curves2,curves2,result.Curves2);
        copy(source.Curves3,curves3,result.Curves3);copy(source.Surfaces3,surfaces,result.Surfaces3);
        copy(source.Triangulations3,meshes,result.Triangulations3);
        return result;
    }
}
