// PDF is a scaled assembly illustration; DXF carries the 1:1 cutting geometry.
namespace {
struct FixtureDXFBoundary {Array Vertices;std::optional<std::array<double,3>> Circle;std::vector<iCAX::GeometryData::NURBS2> Splines;};
FixtureDXFBoundary FixtureExactDXFBoundary(const iCAX::GeometryData::Tube::SRegionLoop2& loop){
    using namespace iCAX::GeometryData;FixtureDXFBoundary result;bool circles=true;double sweep=0;const Circle2* basis=nullptr;
    const bool spline=std::any_of(loop.Curves.begin(),loop.Curves.end(),[](const auto& edge){const auto& c=edge.Segment.Curve;return !std::holds_alternative<Segment2>(c)&&!std::holds_alternative<Circle2>(c)&&!std::holds_alternative<Arc2>(c);});
    if(spline){for(const auto& edge:loop.Curves)result.Splines.push_back(iCAX::Geometry::BoundedPlanarSpline(edge.Segment));
        // DXF has no shared BRep vertices. Certify the actual exported endpoints
        // rather than assuming that topological closure implies curve closure.
        for(size_t i=0;i<result.Splines.size();++i){const auto& a=result.Splines[i].Poles.back();const auto& b=result.Splines[(i+1)%result.Splines.size()].Poles.front();if(std::hypot(a.X-b.X,a.Y-b.Y)>1e-6)throw std::runtime_error("CAM_FIXTURE_DXF_OPEN_PROFILE");}return result;}
    for(const auto& edge:loop.Curves){const auto& segment=edge.Segment;const auto* line=std::get_if<Segment2>(&segment.Curve);
        if(line){const double t=segment.Range.Reversed?segment.Range.Last:segment.Range.First;result.Vertices.emplace_back(Array{line->Start.X+(line->End.X-line->Start.X)*t,line->Start.Y+(line->End.Y-line->Start.Y)*t,0.});circles=false;continue;}
        const auto* circle=std::get_if<Circle2>(&segment.Curve);if(const auto* arc=std::get_if<Arc2>(&segment.Curve))circle=&arc->Basis;
        if(!circle)throw std::invalid_argument("CAM_FIXTURE_DXF_EXACT_CURVE_UNSUPPORTED");
        if(!basis)basis=circle;else circles&=circle->Placement.Location.X==basis->Placement.Location.X&&circle->Placement.Location.Y==basis->Placement.Location.Y&&circle->Radius==basis->Radius;
        const auto& p=circle->Placement;const double first=segment.Range.Reversed?segment.Range.Last:segment.Range.First,delta=(segment.Range.Last-segment.Range.First)*(segment.Range.Reversed?-1:1),hand=p.XDirection.X*p.YDirection.Y-p.XDirection.Y*p.YDirection.X;sweep+=std::abs(delta);
        const auto count=static_cast<size_t>(std::max(1.,std::ceil(std::abs(delta)/std::numbers::pi)));const double step=delta/count;
        for(size_t i=0;i<count;++i){const double angle=first+i*step;result.Vertices.emplace_back(Array{p.Location.X+circle->Radius*(p.XDirection.X*std::cos(angle)+p.YDirection.X*std::sin(angle)),p.Location.Y+circle->Radius*(p.XDirection.Y*std::cos(angle)+p.YDirection.Y*std::sin(angle)),std::tan(step*hand/4)});}
    }
    if(circles&&basis&&std::abs(sweep-2*std::numbers::pi)<1e-12){result.Vertices.clear();result.Circle=std::array<double,3>{basis->Placement.Location.X,basis->Placement.Location.Y,basis->Radius};return result;}
    if(!result.Vertices.empty())return result;throw std::invalid_argument("CAM_FIXTURE_DXF_EXACT_CURVE_UNSUPPORTED");
}
FixtureDXFBoundary FixtureDXFOuter(const Map& part){if(part.contains("exactProfile"))return FixtureExactDXFBoundary(iCAX::CAD::TubeCadJson::Decode<iCAX::GeometryData::Tube::CPlanarRegion2>(part.at("exactProfile")).Boundaries.front());return {part.at("outline").To<Array>(),{}};}
std::vector<FixtureDXFBoundary> FixtureDXFHoles(const Map& part){
    std::vector<FixtureDXFBoundary> result;if(part.contains("exactProfile")){
        const auto profile=iCAX::CAD::TubeCadJson::Decode<iCAX::GeometryData::Tube::CPlanarRegion2>(part.at("exactProfile"));
        if(profile.Boundaries.size()!=part.at("holes").To<Array>().size()+1)throw std::runtime_error("CAM_FIXTURE_PART_PROFILE_ORDER_INVALID");
        for(size_t i=1;i<profile.Boundaries.size();++i)result.push_back(FixtureExactDXFBoundary(profile.Boundaries[i]));
    }else for(const auto& hole:part.at("holes").To<Array>())result.push_back({hole.To<Array>(),{}});return result;
}
Text FixtureAssemblyPDF(const Array& parts,bool interlocked) {
    bool hasLaps=false;for(auto v:parts)for(auto j:v.To<Map>().at("joints").To<Array>())hasLaps|=String(j.To<Map>(),"type")=="LapTop";
    std::vector<Text> objects(3);objects[0]="<< /Type /Catalog /Pages 2 0 R >>";
    objects[2]="<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    std::vector<int> pages;
    auto ordered=parts;std::stable_sort(ordered.begin(),ordered.end(),[](const Variant& a,const Variant& b){return String(a.To<Map>(),"role")=="Base"&&String(b.To<Map>(),"role")!="Base";});
    for(const auto& value:ordered){const auto p=value.To<Map>();std::ostringstream stream;stream.imbue(std::locale::classic());stream<<std::fixed<<std::setprecision(4);
        const auto text=[&](double x,double y,int size,const Text& s){stream<<"BT /F1 "<<size<<" Tf "<<x<<' '<<y<<" Td ("<<s<<") Tj ET\n";};
        text(40,800,20,"Fixture assembly - "+String(p,"number"));
        text(40,770,11,"Units: mm | Quantity: 1 | Thickness: "+std::to_string(p.at("thickness").To<double>()));
        text(40,750,10,interlocked?(hasLaps?"Tab-and-slot base. Fit top-open Y plates, then lower bottom-open X plates.":"Insert each support downward into its base slots. Tenons finish flush underneath."):(hasLaps?"Locate and weld the matching half-lap skeleton frame.":"Locate and weld. Grid crossbars are segmented at rib intersections."));
        text(40,730,10,"Review clamps, beam access, falling parts and weld procedure before use.");
        text(40,710,11,"Scaled illustration. Use the separate 1:1 DXF for cutting.");
        if(interlocked&&String(p,"role")!="Base"){bool bottom=false,top=false;for(auto j:p.at("joints").To<Array>()){bottom|=String(j.To<Map>(),"type")=="LapBottom";top|=String(j.To<Map>(),"type")=="LapTop";}text(40,690,10,bottom?"Assembly group X: bottom-open slots. Insert after group Y.":top?"Assembly group Y: top-open slots. Insert into the base first.":"Match this support number to the base locating outline before insertion.");}
        double x0=1e100,x1=-1e100,y0=1e100,y1=-1e100;
        for(const auto& v:p.at("outline").To<Array>()){auto q=v.To<Array>();x0=std::min(x0,q[0].To<double>());x1=std::max(x1,q[0].To<double>());y0=std::min(y0,q[1].To<double>());y1=std::max(y1,q[1].To<double>());}
        double scale=std::min(500/(x1-x0),530/(y1-y0));
        const auto x=[&](double v){return 45+(v-x0)*scale;};const auto y=[&](double v){return 115+(v-y0)*scale;};
        stream<<"0.15 0.25 0.3 RG 0.8 w\n";bool first=true;
        for(const auto& v:p.at("outline").To<Array>()){auto q=v.To<Array>();stream<<x(q[0].To<double>())<<' '<<y(q[1].To<double>())<<(first?" m\n":" l\n");first=false;}stream<<"h S\n";
        for(const auto& hole:p.at("holes").To<Array>()){stream<<"0.7 0.2 0.1 RG\n";bool start=true;for(auto v:hole.To<Array>()){auto q=v.To<Array>();stream<<x(q[0].To<double>())<<' '<<y(q[1].To<double>())<<(start?" m\n":" l\n");start=false;}stream<<"h S\n";}
        if(String(p,"role")=="Base"){std::vector<std::array<double,4>> labels,holes;for(auto h:p.at("holes").To<Array>()){double a=1e100,b=1e100,c=-1e100,d=-1e100;for(auto v:h.To<Array>()){auto q=v.To<Array>();double px=x(q[0].To<double>()),py=y(q[1].To<double>());a=std::min(a,px);b=std::min(b,py);c=std::max(c,px);d=std::max(d,py);}holes.push_back({a,b,c-a,d-b});}
        for(const auto& v:parts){auto s=v.To<Map>();if(String(s,"role")=="Base")continue;auto f=s.at("frame").To<Array>(),span=s.at("span").To<Array>();double t=s.at("thickness").To<double>();
            stream<<"0.2 0.5 0.6 RG [4 2] 0 d\n";bool start=true;
            for(auto local:std::array<std::array<double,2>,4>{{{span[0].To<double>(),0},{span[1].To<double>(),0},{span[1].To<double>(),t},{span[0].To<double>(),t}}}){
                double wx=f[0].To<double>()*local[0]+f[2].To<double>()*local[1]+f[3].To<double>(),wy=f[4].To<double>()*local[0]+f[6].To<double>()*local[1]+f[7].To<double>();
                stream<<x(wx)<<' '<<y(wy)<<(start?" m\n":" l\n");start=false;}
            stream<<"h S [] 0 d\n";
            const auto number=String(s,"number");const double mid=(span[0].To<double>()+span[1].To<double>())/2;
            const double px=x(f[0].To<double>()*mid+f[2].To<double>()*t/2+f[3].To<double>()),py=y(f[4].To<double>()*mid+f[6].To<double>()*t/2+f[7].To<double>()),width=6.*number.size();
            double lx=px-width/2,ly=py+5;bool placed=false;
            const auto free=[&](double a,double b){if(a<45||a+width>545||b<115||b+10>645)return false;for(const auto& r:labels)if(a<r[0]+r[2]+2&&a+width+2>r[0]&&b-3<r[1]+r[3]+2&&b+12>r[1])return false;for(const auto& r:holes)if(a-1<r[0]+r[2]+1&&a+width+1>r[0]-1&&b-3<r[1]+r[3]+1&&b+11>r[1]-1)return false;return true;};
            for(auto offset:std::array<std::array<double,2>,12>{{{0,5},{0,-15},{8,18},{-8,-28},{12,31},{-12,-41},{24,5},{-24,5},{24,18},{-24,-18},{32,31},{-32,-31}}}){double a=px-width/2+offset[0],b=py+offset[1];if(free(a,b)){lx=a;ly=b;placed=true;break;}}
            if(!placed)for(double b=125;b<635&&!placed;b+=18)for(double a=50;a+width<540;a+=40)if(free(a,b)){lx=a;ly=b;placed=true;break;}
            if(!placed)throw std::runtime_error("CAM_FIXTURE_DRAWING_LABEL_DENSITY");
            if(std::hypot(lx+width/2-px,ly-py)>20)stream<<px<<' '<<py<<" m "<<lx+width/2<<' '<<ly<<" l S\n";
            stream<<"q 1 1 1 rg "<<lx-1<<' '<<ly-3<<' '<<width+2<<" 14 re f Q\n";text(lx,ly,10,number);labels.push_back({lx,ly-3,width,14});
        }}
        text(40,70,11,"Blank width: "+std::to_string(x1-x0)+" | height: "+std::to_string(y1-y0));
        text(40,50,10,String(p,"role")=="Support"?"Nominal conformal contact. Full-thickness BRep envelope; verify fit before use.":(String(p,"role")=="Connection"?"Connection clearance: ":"Base clearance: ")+std::to_string(p.at("minimumDistance").To<double>())+" mm");
        const auto data=stream.str();int content=static_cast<int>(objects.size())+1;objects.push_back("<< /Length "+std::to_string(data.size())+" >>\nstream\n"+data+"endstream");
        int page=static_cast<int>(objects.size())+1;objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R >> >> /Contents "+std::to_string(content)+" 0 R >>");pages.push_back(page);
    }
    Text kids;for(auto page:pages)kids+=std::to_string(page)+" 0 R ";objects[1]="<< /Type /Pages /Count "+std::to_string(pages.size())+" /Kids ["+kids+"] >>";
    std::ostringstream out;out<<"%PDF-1.4\n";std::vector<std::streamoff> offsets;
    for(size_t i=0;i<objects.size();++i){offsets.push_back(out.tellp());out<<i+1<<" 0 obj\n"<<objects[i]<<"\nendobj\n";}
    auto xref=out.tellp();out<<"xref\n0 "<<objects.size()+1<<"\n0000000000 65535 f \n";
    for(auto offset:offsets)out<<std::setfill('0')<<std::setw(10)<<offset<<" 00000 n \n";
    out<<"trailer\n<< /Size "<<objects.size()+1<<" /Root 1 0 R >>\nstartxref\n"<<xref<<"\n%%EOF\n";return out.str();
}
void VerifyFixtureDXF(const iCAX::CAD::ICadDocumentImportService& reader,const std::filesystem::path& file,const Map& part) {
    const auto document=reader.Load(file).Document;
    auto units=document.Header.find("$INSUNITS");if(units==document.Header.end()||units->second.empty()||units->second.front().Value!="4")throw std::runtime_error("CAM_FIXTURE_DXF_UNITS_FAILED");
    auto section=document.Sections.find("ENTITIES");if(section==document.Sections.end())throw std::runtime_error("CAM_FIXTURE_DXF_OUTLINE_FAILED");
    const auto holes=FixtureDXFHoles(part);const auto outside=FixtureDXFOuter(part);
    struct Expected {const FixtureDXFBoundary* Boundary;const iCAX::GeometryData::NURBS2* Spline;const char* Layer;};std::vector<Expected> expectedEntities;
    const auto append=[&](const FixtureDXFBoundary& boundary,const char* layer){if(boundary.Splines.empty())expectedEntities.push_back({&boundary,nullptr,layer});else for(const auto& spline:boundary.Splines)expectedEntities.push_back({&boundary,&spline,layer});};
    append(outside,"CUT_OUTER");for(const auto& hole:holes)append(hole,"CUT_INNER");size_t ordinal=0;
    const auto sameNumber=[](double a,double b,double tolerance){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance;};
    for(const auto& entity:section->second){Text layer;bool closed=false;std::vector<double> xs,ys,bulges,knots,weights,zs;int flags=0,degree=0,knotCount=0,poleCount=0;std::array<double,3> normal{0,0,1};
        double radius=0;for(const auto& group:entity.Values){if(group.Code==8)layer=group.Value;if(group.Code==70){flags=std::stoi(group.Value);closed=(flags&1)!=0;}if(group.Code==10){xs.push_back(std::stod(group.Value));bulges.push_back(0.);}if(group.Code==20)ys.push_back(std::stod(group.Value));if(group.Code==30)zs.push_back(std::stod(group.Value));if(group.Code==40){radius=std::stod(group.Value);knots.push_back(radius);}if(group.Code==41)weights.push_back(std::stod(group.Value));if(group.Code==42&&!bulges.empty())bulges.back()=std::stod(group.Value);if(group.Code==71)degree=std::stoi(group.Value);if(group.Code==72)knotCount=std::stoi(group.Value);if(group.Code==73)poleCount=std::stoi(group.Value);if(group.Code==210)normal[0]=std::stod(group.Value);if(group.Code==220)normal[1]=std::stod(group.Value);if(group.Code==230)normal[2]=std::stod(group.Value);}
        if(layer!="CUT_OUTER"&&layer!="CUT_INNER")continue;if(ordinal>=expectedEntities.size())throw std::runtime_error("CAM_FIXTURE_DXF_OUTLINE_FAILED");
        const auto& expectedEntity=expectedEntities[ordinal++];if(layer!=expectedEntity.Layer)throw std::runtime_error("CAM_FIXTURE_DXF_OUTLINE_FAILED");const auto& boundary=*expectedEntity.Boundary;
        if(expectedEntity.Spline){const auto& s=*expectedEntity.Spline;std::vector<double> expectedKnots;for(size_t i=0;i<s.Knots.size();++i)for(int j=0;j<s.Multiplicities[i];++j)expectedKnots.push_back(s.Knots[i]);
            if(entity.Type!="SPLINE"||flags!=12||degree!=s.Degree||poleCount!=s.Poles.size()||knotCount!=expectedKnots.size()||knots.size()!=expectedKnots.size()||weights.size()!=s.Weights.size()||xs.size()!=s.Poles.size()||ys.size()!=xs.size()||zs.size()!=xs.size()||!sameNumber(normal[0],0,1e-12)||!sameNumber(normal[1],0,1e-12)||!sameNumber(normal[2],1,1e-12))throw std::runtime_error("CAM_FIXTURE_DXF_SPLINE_FAILED");
            for(size_t i=0;i<knots.size();++i)if(!sameNumber(knots[i],expectedKnots[i],1e-12))throw std::runtime_error("CAM_FIXTURE_DXF_DIMENSION_FAILED");
            for(size_t i=0;i<xs.size();++i)if(!sameNumber(xs[i],s.Poles[i].X,1e-9)||!sameNumber(ys[i],s.Poles[i].Y,1e-9)||!sameNumber(zs[i],0,1e-12)||!sameNumber(weights[i],s.Weights[i],1e-12))throw std::runtime_error("CAM_FIXTURE_DXF_DIMENSION_FAILED");continue;
        }
        if(boundary.Circle){const auto circle=*boundary.Circle;if(entity.Type!="CIRCLE"||xs.size()!=1||ys.size()!=1||std::abs(xs[0]-circle[0])>1e-9||std::abs(ys[0]-circle[1])>1e-9||std::abs(radius-circle[2])>1e-9)throw std::runtime_error("CAM_FIXTURE_DXF_DIMENSION_FAILED");continue;}
        const auto& expected=boundary.Vertices;
        if(entity.Type!="LWPOLYLINE"||!closed||xs.size()!=expected.size()||ys.size()!=expected.size())throw std::runtime_error("CAM_FIXTURE_DXF_OUTLINE_FAILED");
        for(size_t i=0;i<expected.size();++i){auto p=expected[i].To<Array>();if(std::abs(xs[i]-p[0].To<double>())>1e-6||std::abs(ys[i]-p[1].To<double>())>1e-6||std::abs(bulges[i]-(p.size()>2?p[2].To<double>():0.))>1e-12)throw std::runtime_error("CAM_FIXTURE_DXF_DIMENSION_FAILED");}
    }
    if(ordinal!=expectedEntities.size())throw std::runtime_error("CAM_FIXTURE_DXF_OUTLINE_FAILED");
}
}
