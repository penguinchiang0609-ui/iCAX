// Evaluation of the existing Coal backend; this is not fixture-generation acceptance.
namespace {
iCAX::GeometryServices::CollisionMesh FixtureBenchmarkMesh(const TopoDS_Shape& shape) {
    iCAX::GeometryServices::CollisionMesh mesh;
    BRepMesh_IncrementalMesh triangulate(shape,.01,false,.1,true);
    if(!triangulate.IsDone())throw std::runtime_error("Benchmark tessellation failed");
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        TopLoc_Location location;auto triangles=BRep_Tool::Triangulation(TopoDS::Face(it.Current()),location);
        if(triangles.IsNull())throw std::runtime_error("Benchmark missing face mesh");
        const auto offset=static_cast<unsigned>(mesh.Vertices.size());
        for(int i=1;i<=triangles->NbNodes();++i){auto p=triangles->Node(i).Transformed(location.Transformation());mesh.Vertices.push_back({p.X(),p.Y(),p.Z()});}
        for(int i=1;i<=triangles->NbTriangles();++i){int a,b,c;triangles->Triangle(i).Get(a,b,c);mesh.Triangles.push_back({offset+a-1,offset+b-1,offset+c-1});}
    }
    // Explicit allowance for this evaluation; not a certified BRep error bound.
    mesh.Padding=.02;return mesh;
}
}
TEST(FixtureThirdParty, CoalCurvedSheetClearanceAndRepeatedQueries) {
    Scene scene;auto service=scene.Services().Resolve<iCAX::GeometryServices::ICollisionDistanceService>();
    const auto begin=std::chrono::steady_clock::now();
    auto mesh=FixtureBenchmarkMesh(FixtureCurvedSheet());auto fixed=service->PrepareMesh(mesh);
    const auto prepared=std::chrono::steady_clock::now();
    // The analytic quadratic patch has minimum z=16 at x=y=0.
    // A 3 mm thick support has its top at z=15.5: true separation is .5 mm.
    auto plate=service->PrepareBoxes({iCAX::GeometryServices::CollisionBox{{0,0,10.5},{1.5,10,5}}});
    auto pose=CollisionIdentity();auto clear=service->LowerBound(fixed,plate,pose,.2);
    EXPECT_GT(clear.LowerBound,.45);EXPECT_LE(clear.LowerBound,.500001);
    pose[11]=.4;EXPECT_EQ(service->LowerBound(fixed,plate,pose,.2).LowerBound,0);
    pose[11]=1.;EXPECT_EQ(service->LowerBound(fixed,plate,pose,.2).LowerBound,0);
    const auto queries=std::chrono::steady_clock::now();
    for(int i=0;i<1000;++i){pose=CollisionIdentity();pose[3]=(i%3-1)*20.;EXPECT_GT(service->LowerBound(fixed,plate,pose,.2).LowerBound,.2);}
    const auto done=std::chrono::steady_clock::now();
    const auto ms=[](auto duration){return std::chrono::duration<double,std::milli>(duration).count();};
    RecordProperty("triangles",static_cast<int>(mesh.Triangles.size()));
    RecordProperty("prepare_ms",std::to_string(ms(prepared-begin)));
    RecordProperty("queries_1000_ms",std::to_string(ms(done-queries)));
    RecordProperty("center_lower_bound_mm",std::to_string(clear.LowerBound));
    std::cout<<"Coal curved sheet: triangles="<<mesh.Triangles.size()<<" prepare_ms="<<ms(prepared-begin)<<" queries_1000_ms="<<ms(done-queries)<<" lower_bound_mm="<<clear.LowerBound<<std::endl;
}
