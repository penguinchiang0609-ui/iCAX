// Closed triangle shells represent occupied material as well as their surface.
// Open meshes intentionally retain triangle-surface collision semantics.
struct MeshVolume {
    struct Bounds {coal::Vec3s Low=coal::Vec3s::Constant(std::numeric_limits<double>::infinity()),High=coal::Vec3s::Constant(-std::numeric_limits<double>::infinity());
        void Add(const coal::Vec3s& p){Low=Low.cwiseMin(p);High=High.cwiseMax(p);}
    } Box;
    struct Node {Bounds Box;std::size_t Begin=0,End=0;int Left=-1,Right=-1;};
    std::vector<coal::Vec3s> Vertices,Probes;
    std::vector<std::shared_ptr<const MeshVolume>> Volumes;
    std::vector<std::array<std::size_t,3>> Triangles;
    std::vector<std::size_t> Order;
    std::vector<Node> Nodes;
    int Build(std::size_t begin,std::size_t end,const std::function<bool()>& cancelled){
        Check(cancelled);Node node;node.Begin=begin;node.End=end;
        for(auto i=begin;i<end;++i)for(auto v:Triangles[Order[i]])node.Box.Add(Vertices[v]);
        const auto index=static_cast<int>(Nodes.size());Nodes.push_back(node);if(end-begin<=12)return index;
        coal::Vec3s::Index axis=0;(node.Box.High-node.Box.Low).maxCoeff(&axis);const auto middle=begin+(end-begin)/2;
        const auto centroid=[&](std::size_t triangle){double value=0;for(auto vertex:Triangles[triangle])value+=Vertices[vertex][axis];return value;};
        std::nth_element(Order.begin()+begin,Order.begin()+middle,Order.begin()+end,[&](auto a,auto b){return centroid(a)<centroid(b);});
        const auto left=Build(begin,middle,cancelled),right=Build(middle,end,cancelled);Nodes[index].Left=left;Nodes[index].Right=right;return index;
    }
    static std::shared_ptr<const MeshVolume> Closed(const std::vector<coal::Vec3s>& input,const std::vector<coal::Triangle>& triangles,const std::function<bool()>& cancelled,
        const std::vector<std::vector<unsigned>>& volumes={},bool declared=false){
        if(!volumes.empty()){auto result=std::make_shared<MeshVolume>();for(const auto& volume:volumes){Check(cancelled);if(volume.empty())throw std::invalid_argument("COLLISION_MESH_VOLUME_INVALID");
                std::set<unsigned> seen;std::vector<coal::Triangle> shell;for(auto index:volume){if(index>=triangles.size()||!seen.insert(index).second)throw std::invalid_argument("COLLISION_MESH_VOLUME_INVALID");shell.push_back(triangles[index]);}
                const auto closed=Closed(input,shell,cancelled,{},true);if(!closed)throw std::invalid_argument("COLLISION_MESH_VOLUME_INVALID");result->Volumes.push_back(closed);result->Probes.insert(result->Probes.end(),closed->Probes.begin(),closed->Probes.end());}
            return result;}
        auto result=std::make_shared<MeshVolume>();std::map<CollisionVector,std::size_t> canonical;std::map<std::size_t,std::size_t> indices;
        for(const auto& triangle:triangles)for(int corner=0;corner<3;++corner){Check(cancelled);const auto i=triangle[corner];if(indices.contains(i))continue;
            const CollisionVector key{input[i][0],input[i][1],input[i][2]};const auto inserted=canonical.emplace(key,result->Vertices.size());indices.emplace(i,inserted.first->second);
            if(inserted.second){result->Vertices.push_back(input[i]);result->Box.Add(input[i]);}}
        std::map<std::pair<std::size_t,std::size_t>,std::size_t> edges;std::vector<std::size_t> parents(result->Vertices.size());std::vector<bool> used(result->Vertices.size());std::iota(parents.begin(),parents.end(),0);
        const auto root=[&](std::size_t vertex){while(parents[vertex]!=vertex){parents[vertex]=parents[parents[vertex]];vertex=parents[vertex];}return vertex;};
        for(const auto& triangle:triangles){Check(cancelled);std::array<std::size_t,3> t{indices[triangle[0]],indices[triangle[1]],indices[triangle[2]]};
            if(t[0]==t[1]||t[1]==t[2]||t[2]==t[0]||(result->Vertices[t[1]]-result->Vertices[t[0]]).cross(result->Vertices[t[2]]-result->Vertices[t[0]]).squaredNorm()==0){if(declared)continue;return {};}
            result->Triangles.push_back(t);for(int edge=0;edge<3;++edge){auto a=t[edge],b=t[(edge+1)%3];used[a]=used[b]=true;parents[root(a)]=root(b);if(a>b)std::swap(a,b);if(++edges[{a,b}]>2&&!declared)return {};}}
        if(result->Triangles.empty()||(!declared&&std::any_of(edges.begin(),edges.end(),[](const auto& edge){return edge.second!=2;})))return {};
        std::set<std::size_t> components;for(std::size_t i=0;i<parents.size();++i)if(used[i]&&components.insert(root(i)).second)result->Probes.push_back(result->Vertices[i]);
        result->Order.resize(result->Triangles.size());std::iota(result->Order.begin(),result->Order.end(),0);result->Nodes.reserve(result->Triangles.size()/4+1);result->Build(0,result->Order.size(),cancelled);return result;
    }
    bool Contains(const coal::Vec3s& point,const std::function<bool()>& cancelled)const{
        if(!Volumes.empty()){for(std::size_t i=0;i<Volumes.size();++i)if(Volumes[i]->Contains(point,cancelled))return true;return false;}
        if((point.array()<Box.Low.array()).any()||(point.array()>Box.High.array()).any())return false;
        // A hit on a triangle edge is retried with a different ray, rather than
        // double-counting adjacent faces or treating numerical ambiguity as clear.
        const coal::Vec3s directions[]={{1,.3713906763541037,.6947465906068658},{.2193687653531,1,.583729415291},{.73194718267,.42189715321,1}};
        for(const auto& direction:directions){std::size_t count=0;bool ambiguous=false;
            // Build halves each range. DFS therefore needs at most one pending
            // sibling per size_t bit, independent of the triangle count.
            std::array<int,std::numeric_limits<std::size_t>::digits+1> pending{};std::size_t size=1;
            while(size&&!ambiguous){Check(cancelled);const auto& node=Nodes[pending[--size]];double near=0,far=std::numeric_limits<double>::infinity();
                for(int axis=0;axis<3;++axis){auto a=(node.Box.Low[axis]-point[axis])/direction[axis],b=(node.Box.High[axis]-point[axis])/direction[axis];if(a>b)std::swap(a,b);near=std::max(near,a);far=std::min(far,b);}
                if(far<near)continue;if(node.Left>=0){if(size+2>pending.size())throw std::runtime_error("COLLISION_MESH_DEPTH_INVALID");pending[size++]=node.Left;pending[size++]=node.Right;continue;}
                for(auto i=node.Begin;i<node.End;++i){const auto& triangle=Triangles[Order[i]];const auto& a=Vertices[triangle[0]];const auto e1=Vertices[triangle[1]]-a,e2=Vertices[triangle[2]]-a;
                    const auto h=direction.cross(e2);const double determinant=e1.dot(h),scale=e1.norm()*e2.norm()*direction.norm();if(std::abs(determinant)<=scale*1e-13)continue;
                    const auto s=point-a;const double u=s.dot(h)/determinant;if(u< -1e-10||u>1+1e-10)continue;const auto q=s.cross(e1);const double v=direction.dot(q)/determinant;if(v< -1e-10||u+v>1+1e-10)continue;
                    const double distance=e2.dot(q)/determinant;if(distance< -1e-10)continue;
                    if(distance<=1e-10||u<=1e-10||v<=1e-10||u+v>=1-1e-10){ambiguous=true;break;}++count;
                }
            }
            if(!ambiguous)return count%2!=0;
        }
        throw std::runtime_error("COLLISION_DISTANCE_UNRESOLVED");
    }
};
