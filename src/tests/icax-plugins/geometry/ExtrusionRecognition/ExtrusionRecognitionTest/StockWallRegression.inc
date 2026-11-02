int StockWallRegression(const fs::path& directory) {
    fs::create_directories(directory);VariantArray checks;
    const auto check=[&](bool passed,const std::string& name){checks.emplace_back(ObjectMap{{"name",name},{"passed",passed}});
        Save(directory/"checks.json",{{"checks",checks}});if(!passed)throw std::runtime_error(name);};
    R::CExtrudeRecognizesService service;R::SFeatureToolpathOptions options;options.bSingleExtrudedStock=true;
    const auto outer=BRepPrimAPI_MakeBox(gp_Pnt(-100,-10,-15),200,20,30).Shape();
    const auto inner=BRepPrimAPI_MakeBox(gp_Pnt(-101,-8,-13),202,16,26).Shape();
    const auto tube=BRepAlgoAPI_Cut(outer,inner).Shape();
    const auto reference=service.ExtractSectionWires(O::ConvertOpenCascadeShapeToBRep(tube,"","",0.001));
    check(reference.bSuccess,"independent nominal hollow stock reference");
    const auto groove=BRepAlgoAPI_Cut(tube,BRepPrimAPI_MakeBox(gp_Pnt(-40,-5,14),80,10,2).Shape()).Shape();
    const auto grooveModel=O::ConvertOpenCascadeShapeToBRep(groove,"","",0.001);
    const auto before=G::Persistence::Serialize(grooveModel);
    const auto classified=service.ClassifySectionFaces(grooveModel,reference,options);
    check(classified.bSuccess,"local pocket classification succeeds");
    check(std::abs(Area(Build(classified.MachiningGeometry))-1348)<1e-5,
        "all four local pocket walls and its complete 800mm2 floor remain geometric machining boundaries");
    check(before==G::Persistence::Serialize(grooveModel),"stock wall proof cannot mutate pocket source geometry");
    const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,-20,0),gp_Dir(0,1,0)),3,40).Shape();
    const auto drilled=O::ConvertOpenCascadeShapeToBRep(BRepAlgoAPI_Cut(tube,hole).Shape(),"","",0.001);
    const auto drilledResult=service.ClassifySectionFaces(drilled,reference,options);
    check(drilledResult.bSuccess&&std::abs(Area(Build(drilledResult.MachiningGeometry))-(368+24*std::acos(-1.)))<1e-5,
        "through-hole walls remain machining; inner-wire joins do not certify stock ends");
    const auto profile=[](double x,double left,double right,double low,double high){BRepBuilderAPI_MakePolygon wire;
        wire.Add(gp_Pnt(x,left,low));wire.Add(gp_Pnt(x,right,low));wire.Add(gp_Pnt(x,right,high));wire.Add(gp_Pnt(x,left,high));wire.Close();return wire.Wire();};
    const auto rampCut=[&](double terminalDepth){BRepOffsetAPI_ThruSections cutter(true,true,1e-8);
        cutter.AddWire(profile(40,-5,5,15,20));cutter.AddWire(profile(101,-5,5,15-terminalDepth*61/60,20));cutter.Build();
        return BRepAlgoAPI_Cut(tube,cutter.Shape()).Shape();};
    const auto ramp=O::ConvertOpenCascadeShapeToBRep(rampCut(.1),"","",0.001);
    const auto rampBefore=G::Persistence::Serialize(ramp);const auto rampResult=service.ClassifySectionFaces(ramp,reference,options);
    check(rampResult.bSuccess&&std::abs(Area(Build(rampResult.MachiningGeometry))-(373+10*std::hypot(60.,.1)))<1e-5,
        "open-ended shallow ramp retains its complete sloped floor, cutting side walls and end cuts");
    check(rampBefore==G::Persistence::Serialize(ramp),"open ramp classification preserves its exact sloped source geometry");
    const auto throughRamp=O::ConvertOpenCascadeShapeToBRep(rampCut(3),"","",0.001);
    const auto throughResult=service.ClassifySectionFaces(throughRamp,reference,options);bool retainedThroughBevel=false;
    const auto throughFaces=Build(throughResult.MachiningGeometry);
    for(TopExp_Explorer it(throughFaces,TopAbs_FACE);it.More();it.Next()){const BRepAdaptor_Surface s(TopoDS::Face(it.Current()));
        if(s.GetType()==GeomAbs_Plane){const auto n=s.Plane().Axis().Direction();retainedThroughBevel=retainedThroughBevel
            ||(std::abs(n.X())>.01&&std::abs(n.X())<.2&&std::abs(n.Z())>.9);}}
    check(throughResult.bSuccess&&retainedThroughBevel,"a sloped face that reaches the opposite stock skin remains a machining bevel");
    const auto loft=[&](bool cavity){BRepOffsetAPI_ThruSections maker(true,true,1e-8);
        const double first=cavity?-101:-100,last=cavity?101:100;
        maker.AddWire(profile(first,(cavity?-8:-10)+.004*(first+100),cavity?8:10,cavity?-13:-15,cavity?13:15));
        maker.AddWire(profile(last,(cavity?-8:-10)+.004*(last+100),cavity?8:10,cavity?-13:-15,cavity?13:15));
        maker.Build();if(!maker.IsDone())throw std::runtime_error("Cannot build independent drafted stock fixture");return maker.Shape();};
    const auto draftedShape=BRepAlgoAPI_Cut(loft(false),loft(true)).Shape();
    const auto drafted=O::ConvertOpenCascadeShapeToBRep(draftedShape,"","",0.001);
    const auto draftedBefore=G::Persistence::Serialize(drafted);
    const auto draftOuter=BRepPrimAPI_MakeBox(gp_Pnt(-100,-9.6,-15),200,19.6,30).Shape();
    const auto draftInner=BRepPrimAPI_MakeBox(gp_Pnt(-101,-7.6,-13),202,15.6,26).Shape();
    const auto draftReference=service.ExtractSectionWires(O::ConvertOpenCascadeShapeToBRep(BRepAlgoAPI_Cut(draftOuter,draftInner).Shape(),"","",0.001));
    const auto draftResult=service.ClassifySectionFaces(drafted,draftReference,options);
    check(draftResult.bSuccess&&draftResult.MachiningGeometry.Faces.size()==2&&draftResult.NonMachiningGeometry.Faces.size()==8,
        "paired drafted stock walls remain skin without increasing angular or distance tolerance");
    check(draftedBefore==G::Persistence::Serialize(drafted),"drafted source surfaces and trims are unchanged");
    check(Relative(Area(Build(draftResult.SplitGeometry)),Area(draftedShape))<1e-8,"drafted stock proof preserves exact source area");
    Save(directory/"checks.json",{{"passed",true},{"checks",checks}});return 0;
}

int StockWall006Regression(const fs::path& source,const fs::path& directory) {
    fs::create_directories(directory);VariantArray checks,evidence;
    const auto check=[&](bool passed,const std::string& name){checks.emplace_back(ObjectMap{{"name",name},{"passed",passed}});
        Save(directory/"checks.json",{{"checks",checks},{"faces",evidence}});if(!passed)throw std::runtime_error(name);};
    std::ifstream input(source,std::ios::binary);const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto original=G::Persistence::Serialize(axis.AlignedGeometry);const auto body=Build(axis.AlignedGeometry);
    TopTools_IndexedMapOfShape faces;TopExp::MapShapes(body,TopAbs_FACE,faces);
    check(faces.Extent()==40,"006 fixture has its original 40 faces");
    const auto section=service.ExtractSectionWires(axis.AlignedGeometry);
    check(section.bSuccess&&section.Wires.size()==1&&!section.Wires.front().bInner,"006 exact nominal section is available");
    BRepBuilderAPI_MakeWire wire;BRep_Builder builder;std::vector<Handle(Geom_Curve)> contourCurves;std::vector<std::pair<double,double>> ranges;
    for(const auto& edge:section.Wires.front().Edges) {
        const auto curve=O::BuildOpenCascadeCurve(LiftCurve(edge.Curve));auto occ=BRepBuilderAPI_MakeEdge(curve,edge.Range.First,edge.Range.Last).Edge();
        for(TopExp_Explorer vertex(occ,TopAbs_VERTEX);vertex.More();vertex.Next())builder.UpdateVertex(TopoDS::Vertex(vertex.Current()),0.001);
        if(edge.bReversed)occ.Reverse();wire.Add(occ);contourCurves.push_back(curve);ranges.emplace_back(edge.Range.First,edge.Range.Last);
    }
    check(wire.IsDone(),"006 nominal section wire connects with the declared section tolerance");
    auto sectionWire=wire.Wire();auto material=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(1,0,0)),sectionWire,false).Face();
    if(BRepTopAdaptor_FClass2d(material,0.001).PerformInfinitePoint()==TopAbs_IN) {
        sectionWire.Reverse();material=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(1,0,0)),sectionWire,false).Face();
    }
    R::SFeatureToolpathOptions options;options.bSingleExtrudedStock=true;
    const auto classified=service.ClassifySectionFaces(axis.AlignedGeometry,section,options);const auto machining=Build(classified.MachiningGeometry);
    check(classified.bSuccess&&Count(machining,TopAbs_FACE)==24&&Count(Build(classified.NonMachiningGeometry),TopAbs_FACE)==16,
        "006 retains 24 machining faces and exactly 16 stock skin faces");
    for(const auto index:{10,14,30,31}) {
        const auto face=TopoDS::Face(faces(index));std::vector<gp_Pnt> skinContacts;double maximumDistance=0;
        for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
            const BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));const auto a=curve.Value(curve.FirstParameter()),b=curve.Value(curve.LastParameter());
            if(curve.GetType()!=GeomAbs_Line||std::abs(a.X()-b.X())<1||std::hypot(a.Y()-b.Y(),a.Z()-b.Z())>1e-6)continue;
            const gp_Pnt p(0,(a.Y()+b.Y())/2,(a.Z()+b.Z())/2);double distance=std::numeric_limits<double>::infinity();
            for(std::size_t c=0;c<contourCurves.size();++c) {
                GeomAPI_ProjectPointOnCurve project(p,contourCurves[c],ranges[c].first,ranges[c].second);
                if(project.NbPoints())distance=std::min(distance,project.LowerDistance());
            }
            maximumDistance=std::max(maximumDistance,distance);if(distance<=0.001)skinContacts.push_back(p);
        }
        check(skinContacts.size()==2,"006 F"+std::to_string(index)+" has two different axial boundaries on the exact stock contour");
        const auto a=skinContacts[0],b=skinContacts[1];const gp_Vec across(a,b);const auto direction=across.Normalized();
        const auto middle=a.Translated(across*.5),outsideA=a.Translated(direction*-.01),outsideB=b.Translated(direction*.01);
        const auto state=[&](const gp_Pnt& p){BRepClass_FaceClassifier classifier(material,p,1e-6);return classifier.State();};
        const bool crossesWall=state(middle)==TopAbs_IN&&state(outsideA)==TopAbs_OUT&&state(outsideB)==TopAbs_OUT;
        check(crossesWall,"006 F"+std::to_string(index)+" spans a full local material interval between two stock skins");
        const auto trace=BRepBuilderAPI_MakeEdge(a,b).Edge();BRepAlgoAPI_Common covered(trace,material);covered.Build();
        GProp_GProps coveredLength;if(covered.IsDone()&&!covered.Shape().IsNull())BRepGProp::LinearProperties(covered.Shape(),coveredLength);
        const bool wholeSpanCovered=covered.IsDone()&&!covered.Shape().IsNull()&&BRepCheck_Analyzer(covered.Shape()).IsValid()
            &&std::abs(coveredLength.Mass()-across.Magnitude())<1e-6;
        check(wholeSpanCovered,"006 F"+std::to_string(index)+" exact line/material intersection covers the entire transverse span");
        GProp_GProps originalMass;BRepGProp::SurfaceProperties(face,originalMass,1e-9);bool retained=false;
        for(TopExp_Explorer it(machining,TopAbs_FACE);it.More();it.Next()) {
            GProp_GProps candidate;BRepGProp::SurfaceProperties(it.Current(),candidate,1e-9);
            retained=retained||(std::abs(candidate.Mass()-originalMass.Mass())<1e-5&&candidate.CentreOfMass().Distance(originalMass.CentreOfMass())<1e-6);
        }
        evidence.emplace_back(ObjectMap{{"sourceFaceIndex",index},{"areaMm2",originalMass.Mass()},
            {"stockContactDistanceMm",maximumDistance},{"localMaterialSpanMm",across.Magnitude()},{"exactMaterialOverlapMm",coveredLength.Mass()},
            {"entireSpanMaterialOverlapVerified",wholeSpanCovered},{"crossesLocalMaterialWall",crossesWall},{"retained",retained}});
        check(retained,"006 original F"+std::to_string(index)+" is retained in the machining partition without trimming");
    }
    check(original==G::Persistence::Serialize(axis.AlignedGeometry),"006 classification leaves original source geometry unchanged");
    check(Relative(Area(Build(classified.SplitGeometry)),Area(body))<1e-8,"006 exact split area is unchanged");
    Save(directory/"checks.json",{{"passed",true},{"checks",checks},{"faces",evidence},{"diagnostics",Strings(classified.Diagnostics)}});return 0;
}
