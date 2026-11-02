// Mesh visibility, not normal classification: a face is suggested when a
// positive projected patch can be reached from -Z without crossing material.
// Trimmed mesh rays suggest accessible patches. A bounded polygon refinement
// catches narrow exposed strips; this editable suggestion is not a complete
// accessibility certificate. Build verifies the resulting bearing geometry.
std::vector<std::uint64_t> LowerEnvelopeFaces(const SectionPlateInput& in,const std::function<bool()>& cancelled) {
    const auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("PLATE_CANCELLED");};check();
    const auto started=std::chrono::steady_clock::now();
    const auto trace=[&](const auto& label){if(std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<"bearing "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<" ms "<<label<<std::endl;};
    if(!in.Geometry)throw std::invalid_argument("PLATE_INPUT_REQUIRED");
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);
    if(!built.bOK)throw std::runtime_error("PLATE_BREP_INVALID");
    // Reconstruction and meshing are private; the source CAD resource and its
    // display triangulation remain unchanged.
    constexpr double deflection=.025,epsilon=1e-7;
    iCAX::FixtureMesh::Tessellate(built.Shape,deflection,cancelled);
    trace("meshed");
    const auto& m=in.Transform;
    struct Triangle {std::uint64_t Face;std::array<coal::Vec3s,3> P;double Area;std::array<double,5> Bounds;std::array<double,2> Center;};
    std::vector<Triangle> triangles;
    auto data=vtkSmartPointer<vtkPolyData>::New();auto points=vtkSmartPointer<vtkPoints>::New();points->SetDataTypeToDouble();auto cells=vtkSmartPointer<vtkCellArray>::New();
    std::map<std::uint64_t,std::vector<size_t>> byFace;
    for(const auto& [id,face]:built.Faces){
        check();const auto mesh=iCAX::FixtureMesh::Collect(face,deflection,cancelled);
        for(const auto& indices:mesh.Triangles){
            if(triangles.size()%1024==0)check();Triangle t;t.Face=id;
            for(int k=0;k<3;++k){const auto& p=mesh.Points[indices[k]];t.P[k]={m[0]*p.x()+m[1]*p.y()+m[2]*p.z()+m[3],m[4]*p.x()+m[5]*p.y()+m[6]*p.z()+m[7],m[8]*p.x()+m[9]*p.y()+m[10]*p.z()+m[11]};}
            const auto normal=(t.P[1]-t.P[0]).cross(t.P[2]-t.P[0]);t.Area=std::abs(normal.z())/2;
            if(t.Area<=epsilon||std::abs(normal.z())<=normal.norm()*1e-6)continue;
            t.Bounds={t.P[0].x(),t.P[0].x(),t.P[0].y(),t.P[0].y(),t.P[0].z()};
            for(const auto& p:t.P){t.Bounds[0]=std::min(t.Bounds[0],p.x());t.Bounds[1]=std::max(t.Bounds[1],p.x());t.Bounds[2]=std::min(t.Bounds[2],p.y());t.Bounds[3]=std::max(t.Bounds[3],p.y());t.Bounds[4]=std::min(t.Bounds[4],p.z());}
            t.Center={(t.P[0].x()+t.P[1].x()+t.P[2].x())/3,(t.P[0].y()+t.P[1].y()+t.P[2].y())/3};
            iCAX::FixtureMesh::CheckMeshBudget((triangles.size()+1)*3,triangles.size()+1);
            vtkIdType ids[3];for(int k=0;k<3;++k)ids[k]=points->InsertNextPoint(t.P[k].data());cells->InsertNextCell(3,ids);
            byFace[id].push_back(triangles.size());triangles.push_back(std::move(t));
        }
    }
    if(triangles.empty())return {};
    data->SetPoints(points);data->SetPolys(cells);
    trace("triangles "+std::to_string(triangles.size()));
    auto locator=vtkSmartPointer<vtkStaticCellLocator>::New();locator->SetDataSet(data);locator->BuildLocator();
    double bounds[6];data->GetBounds(bounds);
    const auto path=[](const Triangle& t){PathD p;for(const auto& q:t.P)p.emplace_back(q.x(),q.y());if(!IsPositive(p))std::reverse(p.begin(),p.end());return p;};
    const auto height=[](const Triangle& t,const PointD& p){const auto n=(t.P[1]-t.P[0]).cross(t.P[2]-t.P[0]);return t.P[0].z()-(n.x()*(p.x-t.P[0].x())+n.y()*(p.y-t.P[0].y()))/n.z();};
    // Projected BVH: vertical visibility is a 2D point query. A general 3D
    // locator's Debug line/triangle intersection was the dominant cost on thin
    // perforated sheet, even though almost all hits are hidden by a low face.
    struct Node {double X0=1e100,X1=-1e100,Y0=1e100,Y1=-1e100,Z0=1e100;size_t Begin=0,End=0,Left=0,Right=0;};
    std::vector<size_t> order;for(size_t i=0;i<triangles.size();++i)order.push_back(i);
    std::vector<Node> nodes;nodes.reserve(2*triangles.size());
    const auto build=[&](auto&& self,size_t begin,size_t end)->size_t{
        check();Node n;n.Begin=begin;n.End=end;
        double cx0=1e100,cx1=-1e100,cy0=1e100,cy1=-1e100;
        for(size_t i=begin;i<end;++i){const auto& t=triangles[order[i]];const auto& b=t.Bounds;n.X0=std::min(n.X0,b[0]);n.X1=std::max(n.X1,b[1]);n.Y0=std::min(n.Y0,b[2]);n.Y1=std::max(n.Y1,b[3]);n.Z0=std::min(n.Z0,b[4]);cx0=std::min(cx0,t.Center[0]);cx1=std::max(cx1,t.Center[0]);cy0=std::min(cy0,t.Center[1]);cy1=std::max(cy1,t.Center[1]);}
        const auto index=nodes.size();nodes.push_back(n);
        // Split by center spread. Long rail triangles span the whole part;
        // splitting repeatedly by their extent creates overlapping subtrees.
        if(end-begin>8){const auto mid=(begin+end)/2;const int axis=cx1-cx0>=cy1-cy0?0:1;
            std::nth_element(order.data()+begin,order.data()+mid,order.data()+end,[&](auto a,auto b){return triangles[a].Center[axis]<triangles[b].Center[axis];});
            nodes[index].Left=self(self,begin,mid);nodes[index].Right=self(self,mid,end);
        }return index;
    };build(build,0,order.size());
    const auto lowerHit=[&](const coal::Vec3s& point){
        double lowest=1e100;size_t first=triangles.size();const auto x=point.x(),y=point.y();
        const auto visit=[&](auto&& self,size_t index)->void{
            const auto& n=nodes[index];if(x<n.X0-1e-9||x>n.X1+1e-9||y<n.Y0-1e-9||y>n.Y1+1e-9||n.Z0>=lowest)return;
            if(n.Left){const auto a=n.Left,b=n.Right;if(nodes[a].Z0<nodes[b].Z0){self(self,a);self(self,b);}else{self(self,b);self(self,a);}return;}
            for(size_t i=n.Begin;i<n.End;++i){const auto& t=triangles[order[i]];const auto& a=t.P[0];const auto& b=t.P[1];const auto& c=t.P[2];
                const double det=(b.y()-c.y())*(a.x()-c.x())+(c.x()-b.x())*(a.y()-c.y());
                const double u=((b.y()-c.y())*(x-c.x())+(c.x()-b.x())*(y-c.y()))/det,v=((c.y()-a.y())*(x-c.x())+(a.x()-c.x())*(y-c.y()))/det;
                if(u< -1e-10||v< -1e-10||u+v>1+1e-10)continue;const double z=u*a.z()+v*b.z()+(1-u-v)*c.z();if(z<lowest){lowest=z;first=order[i];}
            }
        };visit(visit,0);return std::make_pair(lowest,first);
    };
    trace("projected BVH ready");
    auto neighbors=vtkSmartPointer<vtkIdList>::New();std::vector<std::uint64_t> result;
    for(auto& [face,indices]:byFace){
        // Broad patches usually establish visibility on the first query.
        std::sort(indices.data(),indices.data()+indices.size(),[&](auto a,auto b){return triangles[a].Area>triangles[b].Area;});
        // Distribute samples through the face's triangle sizes. Bound the work
        // per face; manual selection is retained for unsampled narrow patches.
        const auto sampleCount=std::min<size_t>(64,indices.size());
        for(size_t rank=0;rank<sampleCount;++rank){
            const auto index=indices[rank*indices.size()/sampleCount];
            check();const auto& t=triangles[index];auto original=path(t);PathsD visible{original},blockers;
            // A first-hit query cheaply establishes a visible patch. A hidden
            // centroid alone cannot reject a triangle: reject only when that
            // single lower triangle covers ALL three vertices. Remaining
            // partial occlusions still use complete polygon subtraction.
            const auto center=((t.P[0]+t.P[1]+t.P[2])/3).eval();
            const auto [hit,first]=lowerHit(center);
            if(first<triangles.size()){
                if(std::abs(hit-center.z())<=epsilon){result.push_back(face);break;}
                const auto& b=triangles[first];const auto boundary=path(b);bool covered=true;
                for(const auto& p:t.P){const PointD q(p.x(),p.y());if(height(t,q)-height(b,q)<=epsilon){covered=false;break;}
                    for(size_t k=0;k<3;++k){const auto a=boundary[k],c=boundary[(k+1)%3];if((c.x-a.x)*(q.y-a.y)-(c.y-a.y)*(q.x-a.x)<-1e-9){covered=false;break;}}
                    if(!covered)break;
                }
                if(covered)continue;
            }
            bool exposed=false;
            for(int vertex=0;vertex<3;++vertex){
                const auto sample=(t.P[vertex]*.999+t.P[(vertex+1)%3]*.0005+t.P[(vertex+2)%3]*.0005).eval();
                if(std::abs(lowerHit(sample).first-sample.z())<=epsilon){exposed=true;break;}
            }
            if(exposed){result.push_back(face);break;}
            // Dense curved faces can contain thousands of hidden triangles.
            // Refine only the two broadest patches instead of performing a
            // complete CAD visibility decomposition just to prepare a draft.
            if(rank>=2||indices.size()>16)continue;
            double box[]{t.P[0].x(),t.P[0].x(),t.P[0].y(),t.P[0].y(),bounds[4]-1,bounds[5]+1};
            for(const auto& p:t.P){box[0]=std::min(box[0],p.x());box[1]=std::max(box[1],p.x());box[2]=std::min(box[2],p.y());box[3]=std::max(box[3],p.y());}
            locator->FindCellsWithinBounds(box,neighbors);
            for(vtkIdType j=0;j<neighbors->GetNumberOfIds();++j){
                if(j%128==0){check();if(!blockers.empty()){visible=Difference(visible,blockers,FillRule::NonZero,6);blockers.clear();}if(visible.empty())break;}
                const auto other=static_cast<size_t>(neighbors->GetId(j));if(other==index)continue;
                const auto& b=triangles[other];auto polygon=path(b);PathD lower;
                // Retain the half of the occluder's projection strictly below
                // the candidate plane. No centroid/normal visibility guess.
                auto previous=polygon.back();double d0=height(t,previous)-height(b,previous)-epsilon;
                for(const auto& current:polygon){const double d1=height(t,current)-height(b,current)-epsilon;
                    if((d0>0)!=(d1>0)){const double f=d0/(d0-d1);lower.emplace_back(previous.x+f*(current.x-previous.x),previous.y+f*(current.y-previous.y));}
                    if(d1>0)lower.push_back(current);previous=current;d0=d1;
                }
                if(lower.size()>=3)blockers.push_back(std::move(lower));
            }
            if(!blockers.empty())visible=Difference(visible,blockers,FillRule::NonZero,6);
            double area=0;for(const auto& p:visible)area+=Area(p);
            if(area>epsilon){result.push_back(face);break;}
        }
        trace("face "+std::to_string(face)+" selected "+std::to_string(result.size()));
    }
    check();return result;
}
