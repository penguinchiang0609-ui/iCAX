namespace product_brep_pipeline_tests {
using iCAX::TubeDesigner::detail::CProductBRepConversionPipeline;
using iCAX::OpenCascade::SBRepConversionInput;
using iCAX::GeometryData::BRepModel;

std::string writeShape(const TopoDS_Shape& shape)
{
    std::ostringstream text;BRepTools::Write(shape,text);return text.str();
}

void expectSameConversion(const BRepModel& actual,const BRepModel& expected,const TopoDS_Shape& original)
{
    EXPECT_EQ(actual.Metadata.Name,expected.Metadata.Name);
    EXPECT_EQ(actual.Metadata.SourceId,expected.Metadata.SourceId);
    EXPECT_EQ(actual.Curves2.size(),expected.Curves2.size());
    EXPECT_EQ(actual.Curves3.size(),expected.Curves3.size());
    EXPECT_EQ(actual.Surfaces3.size(),expected.Surfaces3.size());
    EXPECT_EQ(actual.Vertices.size(),expected.Vertices.size());
    EXPECT_EQ(actual.Edges.size(),expected.Edges.size());
    EXPECT_EQ(actual.Wires.size(),expected.Wires.size());
    EXPECT_EQ(actual.Faces.size(),expected.Faces.size());
    EXPECT_EQ(actual.Shells.size(),expected.Shells.size());
    EXPECT_EQ(actual.Solids.size(),expected.Solids.size());
    ASSERT_EQ(actual.Triangulations3.size(),expected.Triangulations3.size());
    for(std::size_t index=0;index<actual.Triangulations3.size();++index) {
        const auto& a=actual.Triangulations3[index];const auto& e=expected.Triangulations3[index];
        EXPECT_EQ(a.Id,e.Id);EXPECT_EQ(a.Metadata.SourceId,actual.Metadata.SourceId);
        EXPECT_EQ(a.Geometry.Triangles,e.Geometry.Triangles);
        EXPECT_EQ(a.Geometry.TriangleFaceIds,e.Geometry.TriangleFaceIds);
        ASSERT_EQ(a.Geometry.Vertices.size(),e.Geometry.Vertices.size());
        for(std::size_t vertex=0;vertex<a.Geometry.Vertices.size();++vertex) {
            EXPECT_DOUBLE_EQ(a.Geometry.Vertices[vertex].X,e.Geometry.Vertices[vertex].X);
            EXPECT_DOUBLE_EQ(a.Geometry.Vertices[vertex].Y,e.Geometry.Vertices[vertex].Y);
            EXPECT_DOUBLE_EQ(a.Geometry.Vertices[vertex].Z,e.Geometry.Vertices[vertex].Z);
        }
        ASSERT_EQ(a.Geometry.Normals.size(),e.Geometry.Normals.size());
        for(std::size_t normal=0;normal<a.Geometry.Normals.size();++normal) {
            EXPECT_DOUBLE_EQ(a.Geometry.Normals[normal].X,e.Geometry.Normals[normal].X);
            EXPECT_DOUBLE_EQ(a.Geometry.Normals[normal].Y,e.Geometry.Normals[normal].Y);
            EXPECT_DOUBLE_EQ(a.Geometry.Normals[normal].Z,e.Geometry.Normals[normal].Z);
        }
        ASSERT_EQ(a.Geometry.TextureCoordinates.size(),e.Geometry.TextureCoordinates.size());
        for(std::size_t texture=0;texture<a.Geometry.TextureCoordinates.size();++texture) {
            EXPECT_DOUBLE_EQ(a.Geometry.TextureCoordinates[texture].U,e.Geometry.TextureCoordinates[texture].U);
            EXPECT_DOUBLE_EQ(a.Geometry.TextureCoordinates[texture].V,e.Geometry.TextureCoordinates[texture].V);
        }
    }
    for(const auto& face:actual.Faces)EXPECT_EQ(face.Metadata.SourceId,actual.Metadata.SourceId);
    for(const auto& edge:actual.Edges)EXPECT_EQ(edge.Metadata.SourceId,actual.Metadata.SourceId);
    const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(actual);
    ASSERT_TRUE(rebuilt.bOK);
    ASSERT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());
    ExpectRootSelectionGeometryMatches(rebuilt.Shape,original);
    EXPECT_NEAR(RootSelectionShapeVolume(rebuilt.Shape),RootSelectionShapeVolume(original),1.e-6);
}

TEST(ProductBRepConversionPipelineTest, OutOfOrderSharedInputsKeepOrderedMeshesMetadataAndExactMaterial)
{
    const auto box=BRepPrimAPI_MakeBox(30.,20.,10.).Shape();
    const auto curved=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(10.,10.,-1.),gp_Dir(0.,0.,1.)),3.,12.).Shape();
    BRepAlgoAPI_Cut cut;NCollection_List<TopoDS_Shape> arguments,tools;
    arguments.Append(box);tools.Append(curved);cut.SetArguments(arguments);cut.SetTools(tools);
    cut.SetNonDestructive(true);cut.Build();ASSERT_TRUE(cut.IsDone());
    const auto final=cut.Shape();
    std::vector<SBRepConversionInput> inputs{
        {box,"base","pipeline/base"},{final,"final","pipeline/final"},
        {curved,"cutter","pipeline/cutter"},{box,"other base","pipeline/base-again"},
        {final,"other final","pipeline/final-again"}};
    std::vector<std::string> before;for(const auto& input:inputs)before.push_back(writeShape(input.Shape));
    const auto baseline=iCAX::OpenCascade::ConvertOpenCascadeShapesToBRep(inputs,.025,1);
    const auto caller=std::this_thread::get_id();std::atomic<int> active=0,peak=0,calls=0;
    CProductBRepConversionPipeline pipeline(inputs.size(),.025,
        [&](const TopoDS_Shape& shape,const std::string& name,const std::string& source,double tolerance) {
            EXPECT_NE(std::this_thread::get_id(),caller);
            const auto current=++active;peak.store(std::max(peak.load(),current));++calls;
            for(const auto& input:inputs)EXPECT_FALSE(shape.IsPartner(input.Shape));
            auto converted=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,name,source,tolerance);
            --active;return converted;
        });
    for(const auto index:{3u,4u,2u,0u,1u})pipeline.Submit(index,inputs[index]);
    const auto actual=pipeline.Finish();ASSERT_EQ(actual.size(),inputs.size());
    EXPECT_EQ(active.load(),0);EXPECT_EQ(peak.load(),1);EXPECT_EQ(calls.load(),3);
    EXPECT_EQ(pipeline.UniqueInputs(),3u);EXPECT_LE(pipeline.PeakQueued(),2u);
    for(std::size_t index=0;index<inputs.size();++index) {
        SCOPED_TRACE(index);EXPECT_EQ(writeShape(inputs[index].Shape),before[index]);
        expectSameConversion(actual[index],baseline[index],inputs[index].Shape);
    }
}

TEST(ProductBRepConversionPipelineTest, EmptyPipelineFinishesWithoutJobs)
{
    CProductBRepConversionPipeline pipeline(0,.025);
    EXPECT_TRUE(pipeline.Finish().empty());EXPECT_EQ(pipeline.UniqueInputs(),0u);
}

TEST(ProductBRepConversionPipelineTest, ConverterFailureJoinsAndRejectsFurtherSubmission)
{
    const auto shape=BRepPrimAPI_MakeBox(10.,10.,10.).Shape();
    std::atomic<int> active=0,calls=0;
    CProductBRepConversionPipeline pipeline(3,.025,
        [&](const TopoDS_Shape&,const std::string&,const std::string&,double)->BRepModel {
            ++active;++calls;--active;throw std::runtime_error("private conversion failure");
        });
    pipeline.Submit(0,{shape,"first","fail/first"});
    try { (void)pipeline.Finish();FAIL()<<"conversion error was swallowed"; }
    catch(const std::runtime_error& error) { EXPECT_STREQ(error.what(),"private conversion failure"); }
    EXPECT_EQ(active.load(),0);EXPECT_EQ(calls.load(),1);
    EXPECT_THROW(pipeline.Submit(1,{shape,"after failure","fail/after"}),std::runtime_error);
}

TEST(ProductBRepConversionPipelineTest, CallerFailureWaitsForActiveWorkerAndDiscardsQueuedJobs)
{
    const auto shape=BRepPrimAPI_MakeBox(10.,10.,10.).Shape();
    std::promise<void> entered,release;auto enteredFuture=entered.get_future();auto gate=release.get_future().share();
    std::atomic<int> active=0,finished=0,calls=0;
    try {
        CProductBRepConversionPipeline pipeline(3,.025,
            [&](const TopoDS_Shape&,const std::string&,const std::string&,double)->BRepModel {
                ++active;const auto invocation=++calls;
                if(invocation==1){entered.set_value();gate.wait();}
                --active;++finished;return {};
            });
        pipeline.Submit(0,{shape,"active","caller/active"});enteredFuture.wait();
        pipeline.Submit(1,{BRepPrimAPI_MakeBox(11.,10.,10.).Shape(),"queued","caller/queued"});
        release.set_value();throw std::runtime_error("caller validation failure");
    } catch(const std::runtime_error& error) { EXPECT_STREQ(error.what(),"caller validation failure"); }
    // A queued job may have started just before the caller exception reaches
    // the destructor; every started job must nevertheless be quiescent here.
    EXPECT_EQ(active.load(),0);EXPECT_GE(finished.load(),1);EXPECT_EQ(finished.load(),calls.load());
}

TEST(ProductBRepConversionPipelineTest, DISABLED_CurrentProductSerialPipelineEquivalenceBenchmark)
{
    using iCAX::TemplateRuntime::CStandardJsonCodec;
    const auto directory=std::filesystem::current_path()/"output/tests/disassembly-performance";
    const auto read=[&](const std::string& name) {
        std::ifstream stream(directory/name,std::ios::binary);
        if(!stream)throw std::runtime_error("Generate the profiled real product fixture first: "+name);
        const std::string text{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
        return CStandardJsonCodec::Parse(text);
    };
    const auto model=iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(read("neutral-batch.json"));
    const auto selected=read("neutral-batch-roots.json").To<iCAX::Data::ObjectMap>();
    std::vector<std::string> roots;
    for(const auto& value:selected.at("baseRoots").To<iCAX::Data::VariantArray>())roots.push_back(value.To<std::string>());
    for(const auto& value:selected.at("roots").To<iCAX::Data::VariantArray>())roots.push_back(value.To<std::string>());
    iCAX::OpenCascade::SNeutralModelEvaluationOptions options;options.MaximumConcurrency=1;
    options.CombineUnobservedSubtractions=true;options.ReuseRigidPrototypes=true;
    options.IntersectMatchingExtrusions=true;options.ExpandSubtractionToolUnions=true;
    options.SuppressUnusedBooleanHistory=true;
    const auto begin=std::chrono::steady_clock::now();
    const auto baseline=iCAX::OpenCascade::EvaluateNeutralModel(model,roots,options);
    std::vector<SBRepConversionInput> serialInputs;
    for(const auto& key:roots)serialInputs.push_back({baseline.At(key),key,"pipeline-fixture/"+key});
    const auto serialConverted=iCAX::OpenCascade::ConvertOpenCascadeShapesToBRep(serialInputs,.025,1);
    const auto serialDone=std::chrono::steady_clock::now();
    std::map<std::string,std::vector<std::size_t>> indices;
    for(std::size_t index=0;index<roots.size();++index)indices[roots[index]].push_back(index);
    CProductBRepConversionPipeline pipeline(roots.size(),.025);
    options.OnSelectedRootReady=[&](const std::string& key,const TopoDS_Shape& shape) {
        if(!BRepCheck_Analyzer(shape).IsValid())throw std::runtime_error("fixture root invalid");
        for(const auto index:indices.at(key))pipeline.Submit(index,{shape,key,"pipeline-fixture/"+key});
    };
    const auto actual=iCAX::OpenCascade::EvaluateNeutralModel(model,roots,options);
    const auto evaluated=std::chrono::steady_clock::now();
    const auto converted=pipeline.Finish();const auto done=std::chrono::steady_clock::now();
    std::cout<<"[native BRep pipeline] roots="<<roots.size()
        <<" serial_ms="<<std::chrono::duration<double,std::milli>(serialDone-begin).count()
        <<" pipeline_ms="<<std::chrono::duration<double,std::milli>(done-serialDone).count()
        <<" neutral_wall_ms="<<std::chrono::duration<double,std::milli>(evaluated-serialDone).count()
        <<" convert_wait_ms="<<pipeline.FinishWaitMilliseconds()
        <<" convert_work_ms="<<pipeline.ConversionMilliseconds()
        <<" backpressure_ms="<<pipeline.BackpressureMilliseconds()
        <<" peak_queue="<<pipeline.PeakQueued()<<std::endl;
    ASSERT_EQ(converted.size(),serialConverted.size());
    for(std::size_t index=0;index<roots.size();++index) {
        SCOPED_TRACE(roots[index]);
        neutral_batch_evaluation_tests::expectSameMaterial(actual.At(roots[index]),baseline.At(roots[index]),
            RootSelectionShapeVolume(baseline.At(roots[index])));
        expectSameConversion(converted[index],serialConverted[index],baseline.At(roots[index]));
    }
}
}
