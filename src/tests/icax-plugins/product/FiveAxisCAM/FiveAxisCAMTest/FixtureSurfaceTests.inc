TEST(FiveAxisFixtureSurface, PerforationsCannotRaiseAPlateAboveTheUnderlyingTiltedPlane) {
    BRepBuilderAPI_MakePolygon boundary;
    for(auto xy:std::array<std::array<double,2>,4>{{{-50,-60},{50,-60},{50,60},{-50,60}}})boundary.Add(gp_Pnt(xy[0],xy[1],.3*xy[0]+.4*xy[1]));
    boundary.Close();auto face=BRepBuilderAPI_MakeFace(boundary.Wire()).Shape();
    // Each hole removes the low side of a finite plate slab. The remaining
    // material is higher, but the plate must still obey the surface in the hole.
    auto perforated=BRepAlgoAPI_Cut(face,BRepPrimAPI_MakeBox(gp_Pnt(-8,-12,-100),10,24,200).Shape()).Shape();
    perforated=BRepAlgoAPI_Cut(perforated,BRepPrimAPI_MakeBox(gp_Pnt(16,-8,-100),20,10,200).Shape()).Shape();
    // This isolates the neutral ceiling/envelope invariant. Product defaults
    // now reserve opening corridors and reject an unsafe single bearing plate.
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();iCAX::Geometry::SectionPlateInput input;
    input.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(perforated,"Perforated ceiling","generated",.05));
    input.Transform={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};for(const auto& item:input.Geometry->Faces)input.FaceIDs.push_back(item.Id);
    input.Thickness=10;input.AvoidOpeningBoundaries=false;
    for(const auto& family:std::array<Text,2>{"ParallelX","ParallelY"}){
        input.Stations={{0.,-1e100,1e100,family=="ParallelX"}};const auto result=service->Build(input,{});ASSERT_GT(result.size(),1u);const auto& plate=result.front();
        const auto& f=plate.Frame;const auto& outline=plate.Outline;double maximum=-1e100;size_t samples=0,holeSamples=0;
        for(size_t i=0;i<outline.size();++i){const auto a=outline[i],b=outline[(i+1)%outline.size()];
            const int count=std::max(2,static_cast<int>(std::ceil(std::hypot(b[0]-a[0],b[1]-a[1])/.5)));
            for(int j=0;j<=count;++j)for(double depth:{0.,2.5,5.,7.5,10.}){double r=static_cast<double>(j)/count,u=a[0]+(b[0]-a[0])*r,v=a[1]+(b[1]-a[1])*r;
                double x=f[0]*u+f[1]*v+f[2]*depth+f[3],y=f[4]*u+f[5]*v+f[6]*depth+f[7],z=f[11]+v;
                maximum=std::max(maximum,z-(.3*x+.4*y));++samples;if((x>-8&&x<2&&std::abs(y)<12)||(x>16&&x<36&&y>-8&&y<2))++holeSamples;
            }
        }
        EXPECT_LE(maximum,.00002)<<family;EXPECT_GT(holeSamples,0u);EXPECT_FALSE(plate.Contacts.empty());
        RecordProperty(family+"_samples",static_cast<int>(samples));RecordProperty(family+"_maximum_protrusion_mm",std::to_string(maximum));
    }
}

TEST(FiveAxisFixtureSurface, JoineryCannotRaiseCheckedSupportProfilesOrReplaceAnAssembly) {
    Scene scene;File part(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face());Imported(scene,part);
    auto recipe=JointRecipe(scene);recipe["layout"]=Text("Manual");recipe["pattern"]=Text("Parallel");recipe["baseDrop"]=6.;recipe["areaWidth"]=400.;recipe["areaDepth"]=400.;
    recipe["stations"]=Array{Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",-56.}},Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",56.}}};
    recipe=SetPlateRecipe(scene,recipe);const auto first=GeneratePlates(scene);AdoptPlates(scene,first);const auto accepted=Read(scene).at("fixtures");
    recipe["jointWeb"]=50.;recipe["tenonPitch"]=120.;SetPlateRecipe(scene,recipe);
    try{GeneratePlates(scene);FAIL()<<"A high joining web must not pierce the workpiece";}
    catch(const std::exception& e){EXPECT_NE(Text(e.what()).find("PLATE_JOINT_ABOVE_SUPPORT"),Text::npos)<<e.what();}
    EXPECT_EQ(Read(scene).at("fixtures"),accepted);
}

TEST(FiveAxisFixtureSurface, OriginalCoverGridPassesTheFinalJoineryEnvelopeGuard) {
    auto root=std::filesystem::current_path();const auto relative=std::filesystem::path(L"TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs");
    while(!root.empty()&&!std::filesystem::exists(root/relative)){auto parent=root.parent_path();if(parent==root)break;root=parent;}ASSERT_TRUE(std::filesystem::exists(root/relative));
    Scene scene;auto request=FixtureRequest(scene);const auto path=(root/relative).u8string();request["sourcePath"]=Text(reinterpret_cast<const char*>(path.data()),path.size());
    const auto imported=Wait(scene,Invoke(&scene,"CamWorkpiece","Import",request));ASSERT_EQ(imported.at("status").To<Text>(),"Completed");
    const auto state=Read(scene),ref=state.at("workpieces").To<Array>()[0].To<Map>().at("geometryRef").To<Map>();
    iCAX::Geometry::SectionPlateInput input;input.Geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    const auto initial=state.at("placements").To<Array>()[0].To<Map>().at("transform").To<Array>();for(size_t i=0;i<16;++i)input.Transform[i]=initial[i].To<double>();
    for(size_t i=4;i<12;++i)input.Transform[i]*=-1;
    for(const auto& face:input.Geometry->Faces)input.FaceIDs.push_back(face.Id);
    input.Automatic=true;input.Pattern="Grid";input.AreaWidth=240;input.AreaDepth=240;input.JointMode="TabSlot";input.JointClearance=.3;
    const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();input.Stations=service->SuggestStations(input,40);
    const auto parts=service->Build(input,{});ASSERT_EQ(parts.size(),11u);EXPECT_TRUE(parts.back().Supported);
    size_t joints=0;for(const auto& p:parts){joints+=p.Joints.size();EXPECT_FALSE(p.Outline.empty());if(!p.Base)EXPECT_LE(p.Distance,2*p.TopRetreat+.001);}
    EXPECT_GT(joints,40u);RecordProperty("parts",static_cast<int>(parts.size()));RecordProperty("joinery_uses",static_cast<int>(joints));
}

TEST(FiveAxisFixtureSurface, OriginalPerforatedCoverStaysBelowItsSurfacesWhenFlippedAndTilted) {
    auto root=std::filesystem::current_path();const auto relative=std::filesystem::path(L"TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs");
    while(!root.empty()&&!std::filesystem::exists(root/relative)){auto parent=root.parent_path();if(parent==root)break;root=parent;}ASSERT_TRUE(std::filesystem::exists(root/relative));
    Scene scene;auto request=FixtureRequest(scene);const auto path=(root/relative).u8string();request["sourcePath"]=Text(reinterpret_cast<const char*>(path.data()),path.size());
    auto imported=Wait(scene,Invoke(&scene,"CamWorkpiece","Import",request));ASSERT_EQ(imported.at("status").To<Text>(),"Completed");const auto ids=imported.at("result").To<Map>();
    const auto state=Read(scene),ref=state.at("workpieces").To<Array>()[0].To<Map>().at("geometryRef").To<Map>();const auto initial=state.at("placements").To<Array>()[0].To<Map>().at("transform").To<Array>();
    auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*geometry);ASSERT_TRUE(built.bOK);
    gp_Trsf centered;centered.SetValues(initial[0].To<double>(),initial[1].To<double>(),initial[2].To<double>(),initial[3].To<double>(),initial[4].To<double>(),initial[5].To<double>(),initial[6].To<double>(),initial[7].To<double>(),initial[8].To<double>(),initial[9].To<double>(),initial[10].To<double>(),initial[11].To<double>());
    for(int orientation=0;orientation<3;++orientation){gp_Trsf transform;if(orientation)transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0)),std::acos(-1.)+(orientation==2?.23:0));transform.Multiply(centered);
        Array matrix;for(int r=1;r<=3;++r)for(int c=1;c<=4;++c)matrix.emplace_back(transform.Value(r,c));for(double n:{0.,0.,0.,1.})matrix.emplace_back(n);
        auto move=PlacementRequest(scene,ids);move["transform"]=matrix;Invoke(&scene,"CamPlacement","SetTransform",move);
        // Independent OCCT ray/surface intersections, without using the
        // generator's tessellation or projected envelope. Retain outer trims.
        BRep_Builder builder;TopoDS_Compound ceilings;builder.MakeCompound(ceilings);size_t holes=0;
        for(const auto& [id,source]:built.Faces){auto local=source;local.Location(TopLoc_Location());const auto wire=BRepTools::OuterWire(local);BRepBuilderAPI_MakeFace ceiling(BRep_Tool::Surface(local),wire,true);ASSERT_TRUE(ceiling.IsDone());builder.Add(ceilings,ceiling.Face().Moved(source.Location()).Moved(TopLoc_Location(transform)));int count=0;for(TopExp_Explorer w(source,TopAbs_WIRE);w.More();w.Next())++count;holes+=count-1;}
        ASSERT_GT(holes,20u);IntCurvesFace_ShapeIntersector rays;rays.Load(ceilings,1e-7);
        auto recipe=JointRecipe(scene);recipe["areaWidth"]=240.;recipe["areaDepth"]=240.;recipe["spacing"]=40.;recipe=SetPlateRecipe(scene,recipe);
        const auto result=GeneratePlates(scene);ASSERT_EQ(result.at("supportCheck").To<Map>().at("status").To<Text>(),"Supported");size_t samples=0;double maximum=-1e100;
        for(auto value:result.at("parts").To<Array>()){auto p=value.To<Map>();if(p.at("role").To<Text>()=="Base")continue;const auto f=p.at("frame").To<Array>(),outline=p.at("outline").To<Array>();EXPECT_FALSE(p.at("contactPoints").To<Array>().empty());
            for(size_t i=0;i<outline.size();++i){const auto a=outline[i].To<Array>(),b=outline[(i+1)%outline.size()].To<Array>();if(std::min(a[1].To<double>(),b[1].To<double>())<=5.0001)continue;
                const int count=std::max(2,static_cast<int>(std::ceil(std::abs(b[0].To<double>()-a[0].To<double>())/1.5)));
                for(int j=0;j<=count;++j)for(double depth:{0.,p.at("thickness").To<double>()/4,p.at("thickness").To<double>()/2,p.at("thickness").To<double>()*3/4,p.at("thickness").To<double>()}){const double r=static_cast<double>(j)/count,u=a[0].To<double>()+(b[0].To<double>()-a[0].To<double>())*r,z=f[11].To<double>()+a[1].To<double>()+(b[1].To<double>()-a[1].To<double>())*r;
                    const double x=f[0].To<double>()*u+f[2].To<double>()*depth+f[3].To<double>(),y=f[4].To<double>()*u+f[6].To<double>()*depth+f[7].To<double>();
                    rays.Perform(gp_Lin(gp_Pnt(x,y,-10000),gp_Dir(0,0,1)),0,20000);ASSERT_TRUE(rays.IsDone());if(!rays.NbPnt())continue;double ceiling=1e100;for(int k=1;k<=rays.NbPnt();++k)ceiling=std::min(ceiling,rays.Pnt(k).Z());maximum=std::max(maximum,z-ceiling);++samples;
                }
            }
        }
        std::cout<<"Surface orientation "<<orientation<<": "<<samples<<" ray checks, maximum above surface "<<maximum<<" mm"<<std::endl;
        EXPECT_GT(samples,500u);EXPECT_LE(maximum,.00002)<<"orientation "<<orientation;RecordProperty("orientation_"+std::to_string(orientation)+"_samples",static_cast<int>(samples));RecordProperty("orientation_"+std::to_string(orientation)+"_maximum_mm",std::to_string(maximum));
        AdoptPlates(scene,result);Invoke(&scene,"CamFixture","Breakdown",FixtureRequest(scene));
    }
}
