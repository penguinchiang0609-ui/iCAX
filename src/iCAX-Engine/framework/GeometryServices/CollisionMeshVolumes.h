#pragma once
#include "CollisionDistanceService.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>

namespace iCAX::GeometryServices {
// Prove an exhaustive partition into closed, nonzero-volume triangle shells.
// Exact vertex coordinates are welded. Touching shells may share a nonmanifold
// edge; only two-face edges connect the shells, and every final shell must have
// exactly two incident faces per edge. No faces or coordinates are changed.
// The owner decides whether these shells represent independent material volumes
// and may assign the result to CollisionMesh::ClosedVolumes for union semantics.
inline std::vector<std::vector<unsigned>> ProveClosedMeshVolumes(
    const CollisionMesh& mesh,const std::function<bool()>& cancelled={}) {
    const auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("COLLISION_MESH_VOLUME_CANCELLED");};
    if(mesh.Vertices.empty()||mesh.Triangles.empty()||mesh.Triangles.size()>std::numeric_limits<unsigned>::max())
        throw std::invalid_argument("COLLISION_MESH_VOLUME_INVALID");
    using Edge=std::pair<std::size_t,std::size_t>;
    const auto subtract=[](const auto& a,const auto& b){return CollisionVector{a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
    const auto cross=[](const auto& a,const auto& b){return CollisionVector{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
    const auto edgeOf=[](std::size_t a,std::size_t b){return a<b?Edge{a,b}:Edge{b,a};};
    std::map<CollisionVector,std::size_t> canonical;std::vector<std::size_t> vertices;vertices.reserve(mesh.Vertices.size());
    for(const auto& point:mesh.Vertices){check();for(double v:point)if(!std::isfinite(v))throw std::invalid_argument("COLLISION_MESH_VOLUME_NONFINITE");vertices.push_back(canonical.emplace(point,canonical.size()).first->second);}
    std::map<Edge,std::vector<unsigned>> edges;std::vector<std::size_t> parents(mesh.Triangles.size());std::iota(parents.begin(),parents.end(),0);
    const auto root=[&](std::size_t i){while(parents[i]!=i){parents[i]=parents[parents[i]];i=parents[i];}return i;};
    for(unsigned i=0;i<mesh.Triangles.size();++i){check();const auto& t=mesh.Triangles[i];for(auto index:t)if(index>=mesh.Vertices.size())throw std::invalid_argument("COLLISION_MESH_VOLUME_INDEX_INVALID");
        const auto normal=cross(subtract(mesh.Vertices[t[1]],mesh.Vertices[t[0]]),subtract(mesh.Vertices[t[2]],mesh.Vertices[t[0]]));
        if(!std::isfinite(normal[0])||!std::isfinite(normal[1])||!std::isfinite(normal[2])||std::all_of(normal.begin(),normal.end(),[](double v){return v==0;}))throw std::invalid_argument("COLLISION_MESH_VOLUME_DEGENERATE");
        for(int k=0;k<3;++k)edges[edgeOf(vertices[t[k]],vertices[t[(k+1)%3]])].push_back(i);
    }
    for(const auto& [_,faces]:edges){check();if(faces.size()==2)parents[root(faces[0])]=root(faces[1]);}
    std::map<std::size_t,std::vector<unsigned>> groups;for(unsigned i=0;i<mesh.Triangles.size();++i){check();groups[root(i)].push_back(i);}
    std::vector<std::vector<unsigned>> result;result.reserve(groups.size());
    for(auto& [_,triangles]:groups){check();std::map<Edge,std::size_t> shell;double volume=0;
        // A shell-local anchor makes the volume proof invariant under a large
        // translation, without cancellation of absolute-coordinate products.
        const auto origin=mesh.Vertices[mesh.Triangles[triangles.front()][0]];
        for(auto i:triangles){check();const auto& t=mesh.Triangles[i];for(int k=0;k<3;++k)++shell[edgeOf(vertices[t[k]],vertices[t[(k+1)%3]])];
            const auto a=subtract(mesh.Vertices[t[0]],origin),b=subtract(mesh.Vertices[t[1]],origin),c=subtract(mesh.Vertices[t[2]],origin),product=cross(b,c);
            for(int k=0;k<3;++k)volume+=a[k]*product[k];}
        if(!std::isfinite(volume)||volume==0||std::any_of(shell.begin(),shell.end(),[](const auto& edge){return edge.second!=2;}))throw std::invalid_argument("COLLISION_MESH_CLOSED_VOLUMES_REQUIRED");
        result.push_back(std::move(triangles));
    }
    return result;
}
}
