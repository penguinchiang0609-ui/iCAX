// Read-only trim-domain certificates. Include inside the classifier's private
// namespace, after these headers have been included at global scope:
// BRepAdaptor_Curve2d.hxx, BRepAdaptor_Surface.hxx, BRepTools.hxx,
// BRepTools_WireExplorer.hxx, BRep_Tool.hxx, TopExp.hxx, TopoDS.hxx,
// TopoDS_Iterator.hxx, TopoDS_Vertex.hxx, Geom2d_BSplineCurve.hxx,
// Geom2d_BezierCurve.hxx, and <array>/<vector>/<limits>/<cmath>/<algorithm>.
// No area, world-size, meshing-success, or CAD-tolerance threshold is used.

struct SZeroAreaTrimEvidence final
{
    bool bZeroArea = false;
    std::string Proof;
    std::size_t nWireCount = 0;
    std::size_t nEdgeUseCount = 0;
    double dUVRoundoffBudget = 0.0;
    double dMaximumEndpointUVGap = 0.0;
    double dUVAreaRepresentationBound = 0.0;
};

namespace zero_area_trim_detail
{
bool ConstantCoordinate(const BRepAdaptor_Curve2d& curve,const bool uAxis,const double value)
{
    const auto coordinate=[=](const gp_Pnt2d& point){return uAxis?point.X():point.Y();};
    switch(curve.GetType())
    {
    case GeomAbs_Line:
    {
        const auto line=curve.Line();
        return coordinate(line.Location())==value
            &&(uAxis?line.Direction().X():line.Direction().Y())==0.0;
    }
    case GeomAbs_BSplineCurve:
    {
        const auto spline=curve.BSpline();
        for(int i=1;i<=spline->NbPoles();++i)
            if(coordinate(spline->Pole(i))!=value||spline->Weight(i)<=0.0)return false;
        return spline->NbPoles()>0;
    }
    case GeomAbs_BezierCurve:
    {
        const auto bezier=curve.Bezier();
        for(int i=1;i<=bezier->NbPoles();++i)
            if(coordinate(bezier->Pole(i))!=value||bezier->Weight(i)<=0.0)return false;
        return bezier->NbPoles()>0;
    }
    default:return false;
    }
}

bool FiniteAffineSegment(const BRepAdaptor_Curve2d& curve)
{
    if(!std::isfinite(curve.FirstParameter())||!std::isfinite(curve.LastParameter())
        ||curve.FirstParameter()>=curve.LastParameter())return false;
    if(curve.GetType()==GeomAbs_Line)return true;
    if(curve.GetType()==GeomAbs_BSplineCurve)
    {
        const auto spline=curve.BSpline();
        return spline->Degree()==1&&spline->NbPoles()==2&&!spline->IsPeriodic()
            &&spline->Weight(1)>0.0&&spline->Weight(2)>0.0;
    }
    if(curve.GetType()==GeomAbs_BezierCurve)
    {
        const auto bezier=curve.Bezier();
        return bezier->Degree()==1&&bezier->NbPoles()==2
            &&bezier->Weight(1)>0.0&&bezier->Weight(2)>0.0;
    }
    return false;
}
}

bool IsZeroAreaTrimFace(const TopoDS_Face& face,SZeroAreaTrimEvidence* output=nullptr)
{
    SZeroAreaTrimEvidence evidence;
    const auto finish=[&](const bool value)
    {
        evidence.bZeroArea=value;if(output)*output=evidence;return value;
    };
    if(face.IsNull()||BRep_Tool::NaturalRestriction(face))return finish(false);
    try
    {
        double u,U,v,V;BRepTools::UVBounds(face,u,U,v,V);
        if(!std::isfinite(u)||!std::isfinite(U)||!std::isfinite(v)||!std::isfinite(V))return finish(false);
        const BRepAdaptor_Surface surface(face,false);
        const bool collapsedU=u==U&&!surface.IsUPeriodic();
        const bool collapsedV=v==V&&!surface.IsVPeriodic();
        bool constantU=collapsedU,constantV=collapsedV;
        std::vector<TopoDS_Wire> wires;
        std::vector<TopoDS_Edge> orderedEdges;
        bool everyWireOrdered=true;
        for(TopoDS_Iterator children(face,false,true);children.More();children.Next())
        {
            if(children.Value().ShapeType()!=TopAbs_WIRE)continue;
            const auto wire=TopoDS::Wire(children.Value());wires.push_back(wire);
            std::size_t rawCount=0,orderedCount=0;
            for(TopoDS_Iterator edges(wire,false,true);edges.More();edges.Next())
                if(edges.Value().ShapeType()==TopAbs_EDGE)++rawCount;
            for(BRepTools_WireExplorer edges(wire,face);edges.More();edges.Next())
            {
                const auto edge=edges.Current();orderedEdges.push_back(edge);++orderedCount;
                const BRepAdaptor_Curve2d curve(edge,face);
                constantU=constantU&&zero_area_trim_detail::ConstantCoordinate(curve,true,u);
                constantV=constantV&&zero_area_trim_detail::ConstantCoordinate(curve,false,v);
            }
            everyWireOrdered=everyWireOrdered&&rawCount==orderedCount;
        }
        evidence.nWireCount=wires.size();evidence.nEdgeUseCount=orderedEdges.size();
        if(wires.empty()||orderedEdges.empty()||!everyWireOrdered)return finish(false);
        if(constantU||constantV)
        {
            evidence.Proof=constantU?"exactly-collapsed-nonperiodic-U-trim-domain-with-constant-control-coordinate"
                :"exactly-collapsed-nonperiodic-V-trim-domain-with-constant-control-coordinate";
            return finish(true);
        }
        // A closed two-edge wire can still enclose a genuine curved lens.
        // Accept only two affine finite segments sharing the same two native
        // vertices in reverse, with matching UV representatives. A narrow
        // four-edge rectangle or nonlinear two-edge lens is never accepted.
        if(wires.size()!=1||orderedEdges.size()!=2||!BRep_Tool::IsClosed(wires.front())
            ||!wires.front().IsSame(BRepTools::OuterWire(face)))return finish(false);
        std::array<gp_Pnt2d,4> ends;
        std::array<TopoDS_Vertex,4> vertices;
        for(std::size_t i=0;i<2;++i)
        {
            const auto& edge=orderedEdges[i];const BRepAdaptor_Curve2d curve(edge,face);
            if(!zero_area_trim_detail::FiniteAffineSegment(curve))return finish(false);
            if(edge.Orientation()!=TopAbs_FORWARD&&edge.Orientation()!=TopAbs_REVERSED)return finish(false);
            const bool reversed=edge.Orientation()==TopAbs_REVERSED;
            ends[i*2]=curve.Value(reversed?curve.LastParameter():curve.FirstParameter());
            ends[i*2+1]=curve.Value(reversed?curve.FirstParameter():curve.LastParameter());
            TopExp::Vertices(edge,vertices[i*2],vertices[i*2+1],true);
        }
        if(vertices[0].IsNull()||vertices[1].IsNull()||vertices[2].IsNull()||vertices[3].IsNull()
            ||!vertices[0].IsSame(vertices[3])||!vertices[1].IsSame(vertices[2]))return finish(false);
        double scale=0;
        for(const auto& point:ends)scale=std::max({scale,std::abs(point.X()),std::abs(point.Y())});
        // This bound is tied to the stored parameter magnitude and IEEE-754
        // arithmetic, not to CAD tolerance or any fixed model/area cutoff.
        const double roundoff=8.0*std::numeric_limits<double>::epsilon()*scale;
        const double firstGap=ends[0].Distance(ends[3]),secondGap=ends[1].Distance(ends[2]);
        evidence.dUVRoundoffBudget=roundoff;evidence.dMaximumEndpointUVGap=std::max(firstGap,secondGap);
        if(firstGap>roundoff||secondGap>roundoff)return finish(false);
        const double minU=std::min({ends[0].X(),ends[1].X(),ends[2].X(),ends[3].X()});
        const double maxU=std::max({ends[0].X(),ends[1].X(),ends[2].X(),ends[3].X()});
        const double minV=std::min({ends[0].Y(),ends[1].Y(),ends[2].Y(),ends[3].Y()});
        const double maxV=std::max({ends[0].Y(),ends[1].Y(),ends[2].Y(),ends[3].Y()});
        if(std::abs(u-minU)>roundoff||std::abs(U-maxU)>roundoff
            ||std::abs(v-minV)>roundoff||std::abs(V-maxV)>roundoff)return finish(false);
        const double perimeter=ends[0].Distance(ends[1])+ends[2].Distance(ends[3]);
        evidence.dUVAreaRepresentationBound=perimeter*roundoff*2;
        evidence.Proof="two-affine-pcurve-uses-retrace-the-same-native-vertex-pair-within-parameter-roundoff";
        return finish(true);
    }
    catch(const Standard_Failure&){return finish(false);}
}
