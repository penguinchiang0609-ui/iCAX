namespace neutral_batch_evaluation_tests {
using iCAX::Data::ObjectMap;
using iCAX::Data::Variant;
using iCAX::Data::VariantArray;
using iCAX::TemplateRuntime::EGeometryOperator;
using iCAX::TemplateRuntime::SNeutralModel;
using iCAX::OpenCascade::EvaluateNeutralModel;
using iCAX::OpenCascade::SNeutralModelEvaluationOptions;

void addBox(SNeutralModel& model,const std::string& key,
    const gp_Pnt& origin,double x,double y,double z)
{
    std::ostringstream text;
    BRepTools::Write(BRepPrimAPI_MakeBox(origin,x,y,z).Shape(),text);
    model.Geometry.push_back({key,EGeometryOperator::Resource,{},ObjectMap{{"brep",text.str()}}});
}

void addCut(SNeutralModel& model,const std::string& key,
    const std::string& target,const std::string& tool,ObjectMap extra={})
{
    extra["operation"]=std::string("subtract");
    extra["target"]=target;
    extra["tools"]=VariantArray{tool};
    model.Geometry.push_back({key,EGeometryOperator::Boolean,{target,tool},std::move(extra)});
}

SNeutralModel overlappingSlots()
{
    SNeutralModel model;
    addBox(model,"raw",gp_Pnt(0.,0.,0.),30.,10.,10.);
    addBox(model,"tool.a",gp_Pnt(4.,-1.,6.),4.,12.,6.);
    addBox(model,"tool.b",gp_Pnt(6.,-1.,6.),6.,12.,6.);
    addBox(model,"tool.c",gp_Pnt(18.,-1.,4.),4.,12.,8.);
    addCut(model,"first","raw","tool.a");
    addCut(model,"second","first","tool.b");
    addCut(model,"final","second","tool.c");
    return model;
}

void expectSameMaterial(const TopoDS_Shape& actual,const TopoDS_Shape& expected,double volume)
{
    ASSERT_FALSE(actual.IsNull());
    ASSERT_FALSE(expected.IsNull());
    EXPECT_TRUE(BRepCheck_Analyzer(actual).IsValid());
    EXPECT_TRUE(BRepCheck_Analyzer(expected).IsValid());
    EXPECT_NEAR(RootSelectionShapeVolume(actual),volume,1.e-6);
    ExpectRootSelectionGeometryMatches(actual,expected);
    GProp_GProps actualProperties,expectedProperties;
    BRepGProp::VolumeProperties(actual,actualProperties);
    BRepGProp::VolumeProperties(expected,expectedProperties);
    EXPECT_LT(actualProperties.CentreOfMass().Distance(expectedProperties.CentreOfMass()),1.e-7);
    const auto actualInertia=actualProperties.MatrixOfInertia();
    const auto expectedInertia=expectedProperties.MatrixOfInertia();
    for(int row=1;row<=3;++row)for(int column=1;column<=3;++column) {
        // Exact Boolean partitions can integrate the same material in a different
        // order; bound double accumulation roundoff at the inertia's own scale.
        const auto expectedValue=expectedInertia.Value(row,column);
        const auto roundoffTolerance=std::max(1.e-5,
            std::abs(expectedValue)*128.*std::numeric_limits<double>::epsilon());
        EXPECT_NEAR(actualInertia.Value(row,column),expectedValue,roundoffTolerance);
    }
    BRepAlgoAPI_Common common;
    common.SetNonDestructive(true);
    NCollection_List<TopoDS_Shape> arguments,tools;
    arguments.Append(actual);tools.Append(expected);
    common.SetArguments(arguments);
    common.SetTools(tools);
    common.Build();
    ASSERT_TRUE(common.IsDone());
    EXPECT_NEAR(RootSelectionShapeVolume(common.Shape()),volume,1.e-6);
}

TEST(NeutralBatchEvaluationTest, CombinedSubtractionsRetainOverlappingSlotMaterial)
{
    const auto model=overlappingSlots();
    SNeutralModelEvaluationOptions serial;serial.MaximumConcurrency=1;
    const auto original=EvaluateNeutralModel(model,{"final"},serial);
    SNeutralModelEvaluationOptions batch;
    batch.MaximumConcurrency=8;batch.CombineUnobservedSubtractions=true;
    const auto combined=EvaluateNeutralModel(model,{"final"},batch);
    EXPECT_TRUE(original.Geometry.contains("first"));
    EXPECT_TRUE(original.Geometry.contains("second"));
    EXPECT_FALSE(combined.Geometry.contains("first"));
    EXPECT_FALSE(combined.Geometry.contains("second"));
    expectSameMaterial(combined.At("final"),original.At("final"),2440.);
    // The evaluator must not mutate the authored or frozen recipe.
    EXPECT_EQ(model.Geometry.at(6).Inputs.front(),"second");
    EXPECT_EQ(model.Geometry.at(5).Inputs.front(),"first");
}

TEST(NeutralBatchEvaluationTest, MaterialWitnessRemainsAnIntermediateBoundary)
{
    SNeutralModel model;
    addBox(model,"raw",gp_Pnt(0.,0.,0.),30.,10.,10.);
    addBox(model,"split",gp_Pnt(12.,-1.,-1.),2.,12.,12.);
    addBox(model,"right.only",gp_Pnt(20.,-1.,-1.),5.,12.,12.);
    addBox(model,"left.notch",gp_Pnt(4.,-1.,6.),4.,12.,6.);
    addCut(model,"witness","raw","split",ObjectMap{{"keepConnectedTo",VariantArray{1.,1.,1.}}});
    addCut(model,"second","witness","right.only");
    addCut(model,"final","second","left.notch");
    SNeutralModelEvaluationOptions serial;serial.MaximumConcurrency=1;
    const auto original=EvaluateNeutralModel(model,{"final"},serial);
    SNeutralModelEvaluationOptions batch;
    batch.MaximumConcurrency=8;batch.CombineUnobservedSubtractions=true;
    const auto combined=EvaluateNeutralModel(model,{"final"},batch);
    ASSERT_TRUE(combined.Geometry.contains("witness"));
    expectSameMaterial(combined.At("witness"),original.At("witness"),1200.);
    expectSameMaterial(combined.At("final"),original.At("final"),1040.);
    BRepClass3d_SolidClassifier discarded(combined.At("final"),gp_Pnt(28.,1.,1.),1.e-7);
    EXPECT_EQ(discarded.State(),TopAbs_OUT);
}

TEST(NeutralBatchEvaluationTest, ExplicitlySelectedIntermediateStillHasItsOwnResult)
{
    const auto model=overlappingSlots();
    SNeutralModelEvaluationOptions serial;serial.MaximumConcurrency=1;
    const auto original=EvaluateNeutralModel(model,{"first","final"},serial);
    SNeutralModelEvaluationOptions batch;
    batch.MaximumConcurrency=8;batch.CombineUnobservedSubtractions=true;
    const auto combined=EvaluateNeutralModel(model,{"first","final"},batch);
    ASSERT_TRUE(combined.Geometry.contains("first"));
    EXPECT_NO_THROW((void)combined.At("first"));
    expectSameMaterial(combined.At("first"),original.At("first"),2840.);
    expectSameMaterial(combined.At("final"),original.At("final"),2440.);
}

void addRectangle(SNeutralModel& model,const std::string& key,double width,double depth,
    const VariantArray& origin,const VariantArray& xAxis,const VariantArray& yAxis)
{
    VariantArray segments{
        ObjectMap{{"kind",std::string("line")},{"start",VariantArray{0.,0.}},{"end",VariantArray{width,0.}}},
        ObjectMap{{"kind",std::string("line")},{"start",VariantArray{width,0.}},{"end",VariantArray{width,depth}}},
        ObjectMap{{"kind",std::string("line")},{"start",VariantArray{width,depth}},{"end",VariantArray{0.,depth}}},
        ObjectMap{{"kind",std::string("line")},{"start",VariantArray{0.,depth}},{"end",VariantArray{0.,0.}}}};
    ObjectMap contour{{"kind",std::string("path")},{"closed",true},{"segments",std::move(segments)}};
    ObjectMap arguments{
        {"placement",ObjectMap{{"origin",origin},{"xAxis",xAxis},{"yAxis",yAxis}}},
        {"contours",VariantArray{std::move(contour)}}};
    model.Geometry.push_back({key,EGeometryOperator::Profile2D,{},std::move(arguments)});
}

SNeutralModel rotatedSlotPair(double secondSlotWidth=2.,bool witness=false)
{
    SNeutralModel model;
    for(const bool rotated:{false,true}) {
        const std::string prefix=rotated?"b.":"a.";
        const VariantArray x=rotated?VariantArray{0.,1.,0.}:VariantArray{1.,0.,0.};
        const VariantArray y=rotated?VariantArray{0.,0.,1.}:VariantArray{0.,1.,0.};
        const VariantArray vector=rotated?VariantArray{10.,0.,0.}:VariantArray{0.,0.,10.};
        addRectangle(model,prefix+"profile",10.,10.,
            rotated?VariantArray{100.,200.,300.}:VariantArray{0.,0.,0.},x,y);
        model.Geometry.push_back({prefix+"raw",EGeometryOperator::Extrude,{prefix+"profile"},ObjectMap{{"vector",vector}}});
        if(witness) {
            addRectangle(model,prefix+"split.profile",2.,10.,
                rotated?VariantArray{100.,204.,300.}:VariantArray{4.,0.,0.},x,y);
            model.Geometry.push_back({prefix+"split",EGeometryOperator::Extrude,{prefix+"split.profile"},ObjectMap{{"vector",vector}}});
            addCut(model,prefix+"retained",prefix+"raw",prefix+"split",ObjectMap{
                {"keepConnectedTo",rotated?VariantArray{101.,201.,301.}:VariantArray{1.,1.,1.}}});
        }
        const auto width=witness?1.:(rotated?secondSlotWidth:2.);
        const auto start=witness?1.:3.;
        addRectangle(model,prefix+"slot.profile",width,10.,
            rotated?VariantArray{106.,200.+start,300.}:VariantArray{start,0.,6.},x,y);
        model.Geometry.push_back({prefix+"slot",EGeometryOperator::Extrude,{prefix+"slot.profile"},ObjectMap{{"vector",vector}}});
        addCut(model,prefix+"final",prefix+(witness?"retained":"raw"),prefix+"slot");
    }
    return model;
}

TEST(NeutralBatchEvaluationTest, RigidProfilesExtrusionsAndCuttersReuseExactLocalPrototypes)
{
    const auto model=rotatedSlotPair();
    const std::vector<std::string> roots{"a.profile","b.profile","a.raw","b.raw","a.final","b.final"};
    SNeutralModelEvaluationOptions serial;serial.MaximumConcurrency=1;
    const auto original=EvaluateNeutralModel(model,roots,serial);
    SNeutralModelEvaluationOptions batch;batch.MaximumConcurrency=8;
    batch.CombineUnobservedSubtractions=true;batch.ReuseRigidPrototypes=true;
    const auto reused=EvaluateNeutralModel(model,roots,batch);
    EXPECT_TRUE(reused.At("a.profile").IsPartner(reused.At("b.profile")));
    EXPECT_TRUE(reused.At("a.raw").IsPartner(reused.At("b.raw")));
    EXPECT_TRUE(reused.At("a.final").IsPartner(reused.At("b.final")));
    expectSameMaterial(reused.At("a.final"),original.At("a.final"),920.);
    expectSameMaterial(reused.At("b.final"),original.At("b.final"),920.);
    expectSameMaterial(reused.At("b.raw"),original.At("b.raw"),1000.);
}

TEST(NeutralBatchEvaluationTest, RigidReuseKeepsDifferentCutterContoursDistinct)
{
    const auto model=rotatedSlotPair(3.);
    SNeutralModelEvaluationOptions serial;serial.MaximumConcurrency=1;
    const auto original=EvaluateNeutralModel(model,{"a.final","b.final"},serial);
    SNeutralModelEvaluationOptions batch;batch.MaximumConcurrency=8;
    batch.CombineUnobservedSubtractions=true;batch.ReuseRigidPrototypes=true;
    const auto reused=EvaluateNeutralModel(model,{"a.final","b.final"},batch);
    EXPECT_FALSE(reused.At("a.final").IsPartner(reused.At("b.final")));
    expectSameMaterial(reused.At("a.final"),original.At("a.final"),920.);
    expectSameMaterial(reused.At("b.final"),original.At("b.final"),880.);
}

TEST(NeutralBatchEvaluationTest, RigidReuseTransformsMaterialWitnessAndPreservesSelection)
{
    const auto model=rotatedSlotPair(2.,true);
    const std::vector<std::string> roots{"a.retained","b.retained","a.final","b.final"};
    SNeutralModelEvaluationOptions serial;serial.MaximumConcurrency=1;
    const auto original=EvaluateNeutralModel(model,roots,serial);
    SNeutralModelEvaluationOptions batch;batch.MaximumConcurrency=8;
    batch.CombineUnobservedSubtractions=true;batch.ReuseRigidPrototypes=true;
    const auto reused=EvaluateNeutralModel(model,roots,batch);
    expectSameMaterial(reused.At("a.retained"),original.At("a.retained"),400.);
    expectSameMaterial(reused.At("b.retained"),original.At("b.retained"),400.);
    expectSameMaterial(reused.At("a.final"),original.At("a.final"),360.);
    expectSameMaterial(reused.At("b.final"),original.At("b.final"),360.);
}

SNeutralModel matchingExtrusions(double secondPlaneZ=0.,double secondLength=10.,bool extraArgument=false)
{
    SNeutralModel model;
    addRectangle(model,"a.profile",10.,10.,VariantArray{0.,0.,0.},VariantArray{1.,0.,0.},VariantArray{0.,1.,0.});
    addRectangle(model,"b.profile",8.,6.,VariantArray{0.,0.,secondPlaneZ},VariantArray{1.,0.,0.},VariantArray{0.,1.,0.});
    ObjectMap aArguments{{"vector",VariantArray{0.,0.,10.}}},bArguments{{"vector",VariantArray{0.,0.,secondLength}}};
    if(extraArgument){aArguments["unrecognizedExtension"]=true;bArguments["unrecognizedExtension"]=true;}
    model.Geometry.push_back({"a.raw",EGeometryOperator::Extrude,{"a.profile"},std::move(aArguments)});
    model.Geometry.push_back({"b.raw",EGeometryOperator::Extrude,{"b.profile"},std::move(bArguments)});
    model.Geometry.push_back({"common",EGeometryOperator::Boolean,{"a.raw","b.raw"},ObjectMap{{"operation",std::string("intersect")}}});
    return model;
}

TEST(NeutralBatchEvaluationTest, MatchingExtrusionsIntersectExactSectionsWithNegativeVectorAndExtensions)
{
    for(const bool negative:{false,true}) {
        auto model=matchingExtrusions();
        for(auto& node:model.Geometry)if(node.Operator==EGeometryOperator::Extrude) {
            node.Arguments["vector"]=VariantArray{0.,0.,negative?-10.:10.};
            node.Arguments["extendStart"]=2.;node.Arguments["extendEnd"]=3.;
        }
        const auto originalArguments=model.Geometry.front().Arguments;
        SNeutralModelEvaluationOptions serial;serial.MaximumConcurrency=1;
        const auto original=EvaluateNeutralModel(model,{"common"},serial);
        SNeutralModelEvaluationOptions exact;exact.MaximumConcurrency=4;
        exact.IntersectMatchingExtrusions=true;exact.ReuseRigidPrototypes=true;
        const auto optimized=EvaluateNeutralModel(model,{"common"},exact);
        expectSameMaterial(optimized.At("common"),original.At("common"),720.);
        EXPECT_EQ(model.Geometry.front().Arguments,originalArguments);
    }
}

TEST(NeutralBatchEvaluationTest, PlanarIntersectionPreservesSelectedSharedAndUnknownArgumentBoundaries)
{
    for(const bool extraArgument:{false,true}) {
        const auto model=matchingExtrusions(0.,10.,extraArgument);
        SNeutralModelEvaluationOptions baseline;baseline.MaximumConcurrency=1;
        const auto original=EvaluateNeutralModel(model,{"a.raw","common"},baseline);
        SNeutralModelEvaluationOptions exact;exact.MaximumConcurrency=1;exact.IntersectMatchingExtrusions=true;
        const auto selected=EvaluateNeutralModel(model,{"a.raw","common"},exact);
        EXPECT_TRUE(selected.Geometry.contains("a.raw"));
        expectSameMaterial(selected.At("a.raw"),original.At("a.raw"),1000.);
        expectSameMaterial(selected.At("common"),original.At("common"),480.);
        const auto unknown=EvaluateNeutralModel(model,{"common"},exact);
        if(extraArgument)EXPECT_TRUE(unknown.Geometry.contains("a.raw"));
    }
    for(const auto changes:{std::pair{2.,10.},std::pair{0.,15.}}) {
        const auto model=matchingExtrusions(changes.first,changes.second);
        SNeutralModelEvaluationOptions serial;serial.MaximumConcurrency=1;
        const auto original=EvaluateNeutralModel(model,{"common"},serial);
        SNeutralModelEvaluationOptions exact=serial;exact.IntersectMatchingExtrusions=true;
        const auto optimized=EvaluateNeutralModel(model,{"common"},exact);
        EXPECT_TRUE(optimized.Geometry.contains("a.raw"));
        expectSameMaterial(optimized.At("common"),original.At("common"),changes.first==2.?384.:480.);
    }
    auto sharedModel=matchingExtrusions();
    addBox(sharedModel,"slot",gp_Pnt(3.,-1.,6.),2.,12.,6.);
    addCut(sharedModel,"another","a.raw","slot");
    SNeutralModelEvaluationOptions baseline;baseline.MaximumConcurrency=1;
    const auto original=EvaluateNeutralModel(sharedModel,{"common","another"},baseline);
    SNeutralModelEvaluationOptions exact;exact.MaximumConcurrency=1;exact.IntersectMatchingExtrusions=true;
    const auto shared=EvaluateNeutralModel(sharedModel,{"common","another"},exact);
    EXPECT_TRUE(shared.Geometry.contains("a.raw"));
    expectSameMaterial(shared.At("common"),original.At("common"),480.);
    expectSameMaterial(shared.At("another"),original.At("another"),920.);
    for(const auto* argument:{"keepConnectedTo","unrecognizedExtension"}) {
        auto model=matchingExtrusions();
        model.Geometry.back().Arguments[argument]=std::string(argument)=="keepConnectedTo"
            ? Variant(VariantArray{1.,1.,1.}):Variant(true);
        const auto expected=EvaluateNeutralModel(model,{"common"},baseline);
        const auto observed=EvaluateNeutralModel(model,{"common"},exact);
        EXPECT_TRUE(observed.Geometry.contains("a.raw"));
        expectSameMaterial(observed.At("common"),expected.At("common"),480.);
    }
}

TEST(NeutralBatchEvaluationTest, MatchingExtrusionsPreserveHolesWithSlantedNonTangentialVector)
{
    auto model=matchingExtrusions();
    SNeutralModel holeModel;
    addRectangle(holeModel,"hole",4.,4.,VariantArray{0.,0.,0.},VariantArray{1.,0.,0.},VariantArray{0.,1.,0.});
    auto hole=std::get<VariantArray>(holeModel.Geometry.front().Arguments.at("contours").m_Value).front();
    auto& segments=std::get<VariantArray>(std::get<ObjectMap>(hole.m_Value).at("segments").m_Value);
    for(auto& segment:segments)for(const auto* name:{"start","end"}) {
        auto& point=std::get<VariantArray>(std::get<ObjectMap>(segment.m_Value).at(name).m_Value);
        point[0]=point[0].To<double>()+2.;point[1]=point[1].To<double>()+2.;
    }
    std::get<VariantArray>(model.Geometry.front().Arguments.at("contours").m_Value).push_back(std::move(hole));
    for(auto& node:model.Geometry)if(node.Operator==EGeometryOperator::Extrude)
        node.Arguments["vector"]=VariantArray{3.,4.,10.};
    SNeutralModelEvaluationOptions serial;serial.MaximumConcurrency=1;
    const auto original=EvaluateNeutralModel(model,{"common"},serial);
    SNeutralModelEvaluationOptions exact=serial;exact.IntersectMatchingExtrusions=true;
    const auto optimized=EvaluateNeutralModel(model,{"common"},exact);
    EXPECT_FALSE(optimized.Geometry.contains("a.raw"));
    expectSameMaterial(optimized.At("common"),original.At("common"),320.);
}

TEST(NeutralBatchEvaluationTest, MatchingExtrusionIntersectionKeepsDisjointSectionsEmpty)
{
    auto model=matchingExtrusions();
    auto& contours=std::get<VariantArray>(model.Geometry[1].Arguments.at("contours").m_Value);
    for(auto& contour:contours) {
        auto& segments=std::get<VariantArray>(std::get<ObjectMap>(contour.m_Value).at("segments").m_Value);
        for(auto& segment:segments)for(const auto* name:{"start","end"}) {
            auto& point=std::get<VariantArray>(std::get<ObjectMap>(segment.m_Value).at(name).m_Value);
            point[0]=point[0].To<double>()+20.;
        }
    }
    SNeutralModelEvaluationOptions serial;serial.MaximumConcurrency=1;
    const auto original=EvaluateNeutralModel(model,{"common"},serial);
    SNeutralModelEvaluationOptions exact=serial;exact.IntersectMatchingExtrusions=true;
    const auto optimized=EvaluateNeutralModel(model,{"common"},exact);
    EXPECT_NEAR(RootSelectionShapeVolume(original.At("common")),0.,1.e-9);
    EXPECT_NEAR(RootSelectionShapeVolume(optimized.At("common")),0.,1.e-9);
}

SNeutralModel subtractionUnion(bool witness=false)
{
    SNeutralModel model;
    addBox(model,"raw",gp_Pnt(0.,0.,0.),30.,10.,10.);
    addBox(model,"tool.a",gp_Pnt(4.,-1.,6.),4.,12.,6.);
    addBox(model,"tool.b",gp_Pnt(6.,-1.,6.),6.,12.,6.);
    ObjectMap unionArguments{{"operation",std::string("union")}};
    if(witness)unionArguments["keepConnectedTo"]=VariantArray{5.,0.,7.};
    model.Geometry.push_back({"tool.union",EGeometryOperator::Boolean,{"tool.a","tool.b"},std::move(unionArguments)});
    model.Geometry.push_back({"tool.placed",EGeometryOperator::Transform,{"tool.union"},ObjectMap{{"placement",ObjectMap{
        {"origin",VariantArray{1.,0.,0.}},{"xAxis",VariantArray{1.,0.,0.}},
        {"yAxis",VariantArray{0.,1.,0.}},{"zAxis",VariantArray{0.,0.,1.}}}}}});
    addCut(model,"final","raw","tool.placed");
    return model;
}

TEST(NeutralBatchEvaluationTest, LocatedUnionToolExpandsToTheSameOverlappingMultiToolCut)
{
    const auto model=subtractionUnion();
    SNeutralModelEvaluationOptions serial;serial.MaximumConcurrency=1;
    const auto original=EvaluateNeutralModel(model,{"final"},serial);
    SNeutralModelEvaluationOptions exact=serial;exact.ExpandSubtractionToolUnions=true;
    const auto expanded=EvaluateNeutralModel(model,{"final"},exact);
    EXPECT_FALSE(expanded.Geometry.contains("tool.union"));
    expectSameMaterial(expanded.At("final"),original.At("final"),2680.);
}

TEST(NeutralBatchEvaluationTest, ToolUnionKeepsSelectionWitnessSharedAndUnknownArgumentBoundaries)
{
    SNeutralModelEvaluationOptions exact;exact.MaximumConcurrency=1;exact.ExpandSubtractionToolUnions=true;
    for(const bool witness:{false,true}) {
        const auto model=subtractionUnion(witness);
        SNeutralModelEvaluationOptions baseline;baseline.MaximumConcurrency=1;
        const auto original=EvaluateNeutralModel(model,{"tool.union","tool.placed","final"},baseline);
        const auto selected=EvaluateNeutralModel(model,{"tool.union","tool.placed","final"},exact);
        EXPECT_TRUE(selected.Geometry.contains("tool.union"));
        expectSameMaterial(selected.At("tool.union"),original.At("tool.union"),576.);
        expectSameMaterial(selected.At("tool.placed"),original.At("tool.placed"),576.);
        expectSameMaterial(selected.At("final"),original.At("final"),2680.);
        if(witness) {
            const auto retained=EvaluateNeutralModel(model,{"final"},exact);
            EXPECT_TRUE(retained.Geometry.contains("tool.union"));
            expectSameMaterial(retained.At("final"),selected.At("final"),2680.);
        }
    }
    auto unknownModel=subtractionUnion();
    for(auto& node:unknownModel.Geometry)if(node.Key=="tool.union")node.Arguments["unrecognizedExtension"]=true;
    const auto unknown=EvaluateNeutralModel(unknownModel,{"final"},exact);
    EXPECT_TRUE(unknown.Geometry.contains("tool.union"));
    auto sharedModel=subtractionUnion();
    addCut(sharedModel,"another","raw","tool.union");
    SNeutralModelEvaluationOptions baseline;baseline.MaximumConcurrency=1;
    const auto original=EvaluateNeutralModel(sharedModel,{"final","another"},baseline);
    const auto shared=EvaluateNeutralModel(sharedModel,{"final","another"},exact);
    EXPECT_TRUE(shared.Geometry.contains("tool.union"));
    expectSameMaterial(shared.At("final"),original.At("final"),2680.);
    expectSameMaterial(shared.At("another"),original.At("another"),2680.);
}

TEST(NeutralBatchEvaluationTest, RigidReuseRetainsUnknownTransformAndFrameArgumentsAsUnionBoundaries)
{
    for(const bool frameArgument:{false,true}) {
        auto model=subtractionUnion();
        for(auto& node:model.Geometry)if(node.Key=="tool.placed") {
            if(frameArgument)std::get<ObjectMap>(node.Arguments.at("placement").m_Value)["futureCoordinateRule"]=true;
            else node.Arguments["futureMachiningRule"]=std::string("keep-boundary");
        }
        const auto completeArguments=model.Geometry[4].Arguments;
        SNeutralModelEvaluationOptions baseline;baseline.MaximumConcurrency=1;baseline.ReuseRigidPrototypes=true;
        const auto original=EvaluateNeutralModel(model,{"final"},baseline);
        SNeutralModelEvaluationOptions exact=baseline;exact.ExpandSubtractionToolUnions=true;
        const auto optimized=EvaluateNeutralModel(model,{"final"},exact);
        // The complete unknown transform and its union must still execute,
        // rather than be factored away before the new argument guard sees it.
        EXPECT_EQ(optimized.Geometry.size(),original.Geometry.size());
        for(const auto& [key,shape]:original.Geometry) {
            ASSERT_TRUE(optimized.Geometry.contains(key));
            expectSameMaterial(optimized.At(key),shape,RootSelectionShapeVolume(shape));
        }
        EXPECT_EQ(model.Geometry[4].Arguments,completeArguments);
        expectSameMaterial(optimized.At("final"),original.At("final"),2680.);
    }
}

TEST(NeutralBatchEvaluationTest, ExactOperationOptimizationsStillRejectMissingNodesAndDependencyCycles)
{
    for(const bool cycle:{false,true})for(const bool rigidReuse:{false,true}) {
        auto model=subtractionUnion();
        for(auto& node:model.Geometry)if(node.Key=="tool.placed")node.Inputs={cycle?"tool.placed":"missing.tool"};
        SNeutralModelEvaluationOptions exact;exact.MaximumConcurrency=4;
        exact.ReuseRigidPrototypes=rigidReuse;exact.CombineUnobservedSubtractions=true;
        exact.IntersectMatchingExtrusions=true;exact.ExpandSubtractionToolUnions=true;
        EXPECT_THROW((void)EvaluateNeutralModel(model,{"final"},exact),std::invalid_argument);
    }
}

TEST(NeutralBatchEvaluationTest, SelectedRootCallbackPublishesAuthoredAliasesOnceOnCaller)
{
    auto model=rotatedSlotPair();
    model.Geometry.push_back({"unused.invalid",EGeometryOperator::Resource,{},ObjectMap{{"brep",std::string("invalid")}}});
    const auto caller=std::this_thread::get_id();
    const std::vector<std::string> roots{"a.profile","b.profile","a.final","b.final","a.final"};
    std::map<std::string,TopoDS_Shape> observed;
    std::map<std::string,std::size_t> calls;
    SNeutralModelEvaluationOptions options;options.MaximumConcurrency=4;options.ReuseRigidPrototypes=true;
    options.OnSelectedRootReady=[&](const std::string& key,const TopoDS_Shape& shape) {
        EXPECT_EQ(std::this_thread::get_id(),caller);
        EXPECT_FALSE(shape.IsNull());
        EXPECT_TRUE(std::find(roots.begin(),roots.end(),key)!=roots.end());
        ++calls[key];observed.emplace(key,shape);
    };
    const auto actual=EvaluateNeutralModel(model,roots,options);
    ASSERT_EQ(calls.size(),4u);
    for(const auto& [key,count]:calls) {
        SCOPED_TRACE(key);
        EXPECT_EQ(count,1u);
        EXPECT_TRUE(actual.Geometry.contains(key));
        EXPECT_TRUE(observed.at(key).IsEqual(actual.At(key)));
    }
    EXPECT_TRUE(actual.At("a.profile").IsPartner(actual.At("b.profile")));
    EXPECT_FALSE(actual.Geometry.contains("unused.invalid"));
    EXPECT_FALSE(observed.contains("a.raw"));
    options.OnSelectedRootReady={};
    const auto expected=EvaluateNeutralModel(model,roots,options);
    ASSERT_EQ(actual.Geometry.size(),expected.Geometry.size());
    expectSameMaterial(actual.At("a.final"),expected.At("a.final"),RootSelectionShapeVolume(expected.At("a.final")));
    expectSameMaterial(actual.At("b.final"),expected.At("b.final"),RootSelectionShapeVolume(expected.At("b.final")));
}

TEST(NeutralBatchEvaluationTest, SelectedRootCallbackRejectsInvalidGraphsBeforeNotification)
{
    for(const bool cycle:{false,true})for(const bool reuse:{false,true}) {
        auto model=overlappingSlots();
        if(cycle)addCut(model,"broken","broken","tool.a");
        else addCut(model,"broken","missing","tool.a");
        std::size_t notified=0;
        SNeutralModelEvaluationOptions options;options.MaximumConcurrency=4;options.ReuseRigidPrototypes=reuse;
        options.OnSelectedRootReady=[&](const std::string&,const TopoDS_Shape&) {++notified;};
        EXPECT_THROW(EvaluateNeutralModel(model,{"raw","broken"},options),std::invalid_argument);
        EXPECT_EQ(notified,0u);
    }
}

TEST(NeutralBatchEvaluationTest, SelectedRootCallbackExceptionPropagatesAfterWorkersJoin)
{
    auto model=overlappingSlots();
    std::vector<std::string> roots{"raw"};
    for(int index=0;index<12;++index) {
        const auto key="parallel."+std::to_string(index);
        addBox(model,key,gp_Pnt(100.+40.*index,0.,0.),30.,20.,10.);
        roots.push_back(key);
    }
    const auto caller=std::this_thread::get_id();
    for(int attempt=0;attempt<8;++attempt) {
        std::size_t notified=0;
        SNeutralModelEvaluationOptions options;options.MaximumConcurrency=4;
        options.OnSelectedRootReady=[&](const std::string&,const TopoDS_Shape&) {
            EXPECT_EQ(std::this_thread::get_id(),caller);++notified;
            throw std::runtime_error("root callback failure");
        };
        try {
            (void)EvaluateNeutralModel(model,roots,options);
            FAIL()<<"callback exception was swallowed";
        } catch(const std::runtime_error& error) {
            EXPECT_STREQ(error.what(),"root callback failure");
        }
        EXPECT_EQ(notified,1u);
    }
    // Reuse the same bounded executor immediately after the throwing call.
    SNeutralModelEvaluationOptions options;options.MaximumConcurrency=4;
    const auto recovered=EvaluateNeutralModel(model,roots,options);
    for(const auto& key:roots)EXPECT_FALSE(recovered.At(key).IsNull());
}

TEST(NeutralBatchEvaluationTest, UnusedBooleanHistoryPreservesEverySelectedShapeAndWitness)
{
    auto model=overlappingSlots();
    model.Geometry.push_back({"union",EGeometryOperator::Boolean,{"tool.b","tool.c"},
        ObjectMap{{"operation",std::string("union")}}});
    model.Geometry.push_back({"common",EGeometryOperator::Boolean,{"union","raw"},
        ObjectMap{{"operation",std::string("intersect")}}});
    model.Geometry.push_back({"multi",EGeometryOperator::Boolean,{"raw","tool.a","tool.b","tool.c"},
        ObjectMap{{"operation",std::string("subtract")}}});
    addBox(model,"split",gp_Pnt(12.,-1.,-1.),2.,12.,12.);
    addCut(model,"witness","raw","split",ObjectMap{{"keepConnectedTo",VariantArray{5.,5.,5.}}});
    const std::vector<std::string> roots{"first","second","final","union","common","multi","witness"};
    SNeutralModelEvaluationOptions baseline;baseline.MaximumConcurrency=1;
    const auto expected=EvaluateNeutralModel(model,roots,baseline);
    SNeutralModelEvaluationOptions noHistory=baseline;noHistory.SuppressUnusedBooleanHistory=true;
    const auto actual=EvaluateNeutralModel(model,roots,noHistory);
    ASSERT_EQ(actual.Geometry.size(),expected.Geometry.size());
    for(const auto& [key,shape]:expected.Geometry) {
        SCOPED_TRACE(key);
        ASSERT_TRUE(actual.Geometry.contains(key));
        expectSameMaterial(actual.At(key),shape,RootSelectionShapeVolume(shape));
    }
    EXPECT_NEAR(RootSelectionShapeVolume(actual.At("witness")),1200.,1.e-6);
    EXPECT_EQ(model.Geometry.back().Arguments.at("keepConnectedTo"),Variant(VariantArray{5.,5.,5.}));
}

TEST(NeutralBatchEvaluationTest, OrientedBoundsPreserveCurvedToolsAndSelectedWitnesses)
{
    auto model=overlappingSlots();
    std::ostringstream text;
    BRepTools::Write(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(8.,-5.,5.),gp_Dir(.3,1.,.2)),2.,25.).Shape(),text);
    model.Geometry.push_back({"curved",EGeometryOperator::Resource,{},ObjectMap{{"brep",text.str()}}});
    model.Geometry.push_back({"oblique.cut",EGeometryOperator::Boolean,{"raw","curved","tool.a"},
        ObjectMap{{"operation",std::string("subtract")}}});
    model.Geometry.push_back({"curved.union",EGeometryOperator::Boolean,{"curved","tool.b"},
        ObjectMap{{"operation",std::string("union")}}});
    model.Geometry.push_back({"curved.common",EGeometryOperator::Boolean,{"raw","curved.union"},
        ObjectMap{{"operation",std::string("intersect")}}});
    addBox(model,"split",gp_Pnt(12.,-1.,-1.),2.,12.,12.);
    addCut(model,"witness","oblique.cut","split",ObjectMap{{"keepConnectedTo",VariantArray{2.,5.,5.}}});
    const std::vector<std::string> roots{"first","final","oblique.cut","curved.union","curved.common","witness"};
    SNeutralModelEvaluationOptions baseline;baseline.MaximumConcurrency=1;
    baseline.SuppressUnusedBooleanHistory=true;
    const auto expected=EvaluateNeutralModel(model,roots,baseline);
    SNeutralModelEvaluationOptions oriented=baseline;oriented.UseOrientedBoundingBoxes=true;
    const auto actual=EvaluateNeutralModel(model,roots,oriented);
    ASSERT_EQ(actual.Geometry.size(),expected.Geometry.size());
    for(const auto& [key,shape]:expected.Geometry) {
        SCOPED_TRACE(key);
        ASSERT_TRUE(actual.Geometry.contains(key));
        expectSameMaterial(actual.At(key),shape,RootSelectionShapeVolume(shape));
    }
    EXPECT_EQ(model.Geometry.back().Arguments.at("keepConnectedTo"),Variant(VariantArray{2.,5.,5.}));
}

TEST(NeutralBatchEvaluationTest, DISABLED_CurrentProductOrientedBoundsBenchmark)
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
    const auto selected=read("neutral-batch-roots.json").To<ObjectMap>();
    std::vector<std::string> roots;
    for(const auto& value:selected.at("roots").To<VariantArray>())roots.push_back(value.To<std::string>());
    SNeutralModelEvaluationOptions baseline;baseline.MaximumConcurrency=1;
    baseline.CombineUnobservedSubtractions=true;baseline.ReuseRigidPrototypes=true;
    baseline.IntersectMatchingExtrusions=true;baseline.ExpandSubtractionToolUnions=true;
    baseline.SuppressUnusedBooleanHistory=true;
    const auto start=std::chrono::steady_clock::now();
    const auto expected=EvaluateNeutralModel(model,roots,baseline);
    const auto originalDone=std::chrono::steady_clock::now();
    SNeutralModelEvaluationOptions oriented=baseline;oriented.UseOrientedBoundingBoxes=true;
    const auto actual=EvaluateNeutralModel(model,roots,oriented);
    const auto orientedDone=std::chrono::steady_clock::now();
    std::cout<<"[native oriented bounds] roots="<<roots.size()
        <<" axis_ms="<<std::chrono::duration<double,std::milli>(originalDone-start).count()
        <<" oriented_ms="<<std::chrono::duration<double,std::milli>(orientedDone-originalDone).count()<<std::endl;
    ASSERT_EQ(actual.Geometry.size(),expected.Geometry.size());
    std::vector<iCAX::OpenCascade::SBRepConversionInput> inputs;
    for(const auto& key:roots)inputs.push_back({actual.At(key),key,"benchmark/oriented/"+key});
    const auto converted=iCAX::OpenCascade::ConvertOpenCascadeShapesToBRep(inputs,.025,1);
    ASSERT_EQ(converted.size(),roots.size());
    for(std::size_t index=0;index<roots.size();++index) {
        SCOPED_TRACE(roots[index]);
        const auto& original=expected.At(roots[index]);
        expectSameMaterial(actual.At(roots[index]),original,RootSelectionShapeVolume(original));
        const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(converted[index]);
        ASSERT_TRUE(rebuilt.bOK);
        ExpectRootSelectionGeometryMatches(rebuilt.Shape,original);
    }
}

TEST(NeutralBatchEvaluationTest, DISABLED_CurrentProductUnusedHistoryBenchmark)
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
    const auto selected=read("neutral-batch-roots.json").To<ObjectMap>();
    std::vector<std::string> roots;
    for(const auto& value:selected.at("baseRoots").To<VariantArray>())roots.push_back(value.To<std::string>());
    for(const auto& value:selected.at("roots").To<VariantArray>())roots.push_back(value.To<std::string>());
    SNeutralModelEvaluationOptions baseline;baseline.MaximumConcurrency=1;
    baseline.CombineUnobservedSubtractions=true;baseline.ReuseRigidPrototypes=true;
    baseline.IntersectMatchingExtrusions=true;baseline.ExpandSubtractionToolUnions=true;
    const auto start=std::chrono::steady_clock::now();
    const auto expected=EvaluateNeutralModel(model,roots,baseline);
    const auto originalDone=std::chrono::steady_clock::now();
    SNeutralModelEvaluationOptions noHistory=baseline;noHistory.SuppressUnusedBooleanHistory=true;
    const auto actual=EvaluateNeutralModel(model,roots,noHistory);
    const auto noHistoryDone=std::chrono::steady_clock::now();
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> sameFaces,partnerFaces;
    std::size_t faceOccurrences=0;
    for(const auto& key:roots)
        for(TopExp_Explorer face(actual.At(key),TopAbs_FACE);face.More();face.Next()) {
            ++faceOccurrences;
            sameFaces.Add(face.Current());
            partnerFaces.Add(face.Current().Located(TopLoc_Location()));
        }
    std::cout<<"[native unused Boolean history] roots="<<roots.size()
        <<" history_ms="<<std::chrono::duration<double,std::milli>(originalDone-start).count()
        <<" no_history_ms="<<std::chrono::duration<double,std::milli>(noHistoryDone-originalDone).count()
        <<" face_occurrences="<<faceOccurrences<<" same_faces="<<sameFaces.Extent()
        <<" partner_faces="<<partnerFaces.Extent()<<std::endl;
    ASSERT_EQ(actual.Geometry.size(),expected.Geometry.size());
    std::vector<iCAX::OpenCascade::SBRepConversionInput> inputs;
    for(const auto& key:roots)inputs.push_back({actual.At(key),key,"benchmark/no-history/"+key});
    const auto converted=iCAX::OpenCascade::ConvertOpenCascadeShapesToBRep(inputs,.025,1);
    ASSERT_EQ(converted.size(),roots.size());
    for(std::size_t index=0;index<roots.size();++index) {
        SCOPED_TRACE(roots[index]);
        const auto& original=expected.At(roots[index]);
        expectSameMaterial(actual.At(roots[index]),original,RootSelectionShapeVolume(original));
        const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(converted[index]);
        ASSERT_TRUE(rebuilt.bOK);
        ExpectRootSelectionGeometryMatches(rebuilt.Shape,original);
    }
}

TEST(NeutralBatchEvaluationTest, DISABLED_CurrentProductExactOperationBenchmark)
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
    const auto selected=read("neutral-batch-roots.json").To<ObjectMap>();
    std::vector<std::string> roots;
    for(const auto& value:selected.at("roots").To<VariantArray>())roots.push_back(value.To<std::string>());
    SNeutralModelEvaluationOptions baseline;baseline.MaximumConcurrency=1;
    baseline.CombineUnobservedSubtractions=true;baseline.ReuseRigidPrototypes=true;
    const auto start=std::chrono::steady_clock::now();
    const auto original=EvaluateNeutralModel(model,roots,baseline);
    const auto originalEnd=std::chrono::steady_clock::now();
    SNeutralModelEvaluationOptions exact=baseline;
    exact.IntersectMatchingExtrusions=true;exact.ExpandSubtractionToolUnions=true;
    exact.SuppressUnusedBooleanHistory=true;
    const auto optimized=EvaluateNeutralModel(model,roots,exact);
    const auto optimizedEnd=std::chrono::steady_clock::now();
    std::vector<iCAX::OpenCascade::SBRepConversionInput> inputs;
    for(const auto& key:roots)inputs.push_back({optimized.At(key),key,"benchmark/exact/"+key});
    const auto converted=iCAX::OpenCascade::ConvertOpenCascadeShapesToBRep(inputs,.025,1);
    const auto finished=std::chrono::steady_clock::now();
    std::cout<<"[native exact operations] roots="<<roots.size()
        <<" baseline_ms="<<std::chrono::duration<double,std::milli>(originalEnd-start).count()
        <<" exact_ms="<<std::chrono::duration<double,std::milli>(optimizedEnd-originalEnd).count()
        <<" convert_ms="<<std::chrono::duration<double,std::milli>(finished-optimizedEnd).count()<<std::endl;
    ASSERT_EQ(converted.size(),roots.size());
    for(std::size_t index=0;index<roots.size();++index) {
        SCOPED_TRACE(roots[index]);
        const auto& expected=original.At(roots[index]);
        expectSameMaterial(optimized.At(roots[index]),expected,RootSelectionShapeVolume(expected));
        const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(converted[index]);
        ASSERT_TRUE(rebuilt.bOK);
        ExpectRootSelectionGeometryMatches(rebuilt.Shape,expected);
    }
}

TEST(NeutralBatchEvaluationTest, DISABLED_CurrentProductNativeConcurrencyBenchmark)
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
    const auto selected=read("neutral-batch-roots.json").To<ObjectMap>();
    std::vector<std::string> roots;
    for(const auto& value:selected.at("baseRoots").To<VariantArray>())roots.push_back(value.To<std::string>());
    for(const auto& value:selected.at("roots").To<VariantArray>())roots.push_back(value.To<std::string>());
    ASSERT_GE(selected.at("roots").To<VariantArray>().size(),10u);
    ASSERT_GE(model.Geometry.size(),100u);
    std::vector<TopoDS_Shape> expected;
    VariantArray measurements;
    for(const std::size_t workers:{1u,2u,4u,8u}) {
        SCOPED_TRACE(std::to_string(workers));
        SNeutralModelEvaluationOptions options;
        options.MaximumConcurrency=workers;
        options.CombineUnobservedSubtractions=true;
        options.ReuseRigidPrototypes=true;
        const auto start=std::chrono::steady_clock::now();
        const auto evaluated=EvaluateNeutralModel(model,roots,options);
        const auto solved=std::chrono::steady_clock::now();
        std::vector<iCAX::OpenCascade::SBRepConversionInput> inputs;
        for(const auto& key:roots)inputs.push_back({evaluated.At(key),key,"benchmark/"+key});
        const auto converted=iCAX::OpenCascade::ConvertOpenCascadeShapesToBRep(inputs,.025,workers);
        const auto finished=std::chrono::steady_clock::now();
        const auto solveMs=std::chrono::duration<double,std::milli>(solved-start).count();
        const auto convertMs=std::chrono::duration<double,std::milli>(finished-solved).count();
        std::cout<<"[native concurrency] workers="<<workers<<" roots="<<roots.size()
            <<" solve_ms="<<solveMs<<" convert_ms="<<convertMs
            <<" total_ms="<<(solveMs+convertMs)<<std::endl;
        measurements.emplace_back(ObjectMap{{"workers",static_cast<unsigned long long>(workers)},
            {"solveMs",solveMs},{"convertMs",convertMs},{"totalMs",solveMs+convertMs}});
        ASSERT_EQ(converted.size(),roots.size());
        for(std::size_t index=0;index<roots.size();++index) {
            SCOPED_TRACE(roots[index]);
            const auto& shape=evaluated.At(roots[index]);
            EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid());
            EXPECT_GT(RootSelectionShapeVolume(shape),0.);
            if(workers==1)expected.push_back(shape);
            else ExpectRootSelectionGeometryMatches(shape,expected.at(index));
            const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(converted[index]);
            ASSERT_TRUE(rebuilt.bOK);
            ExpectRootSelectionGeometryMatches(rebuilt.Shape,shape);
        }
    }
    std::ofstream(directory/"native-concurrency.json")<<CStandardJsonCodec::Serialize(
        ObjectMap{{"roots",static_cast<unsigned long long>(roots.size())},{"measurements",std::move(measurements)}});
}

}
