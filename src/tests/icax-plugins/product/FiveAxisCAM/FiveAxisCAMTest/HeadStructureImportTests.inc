namespace {
    struct HeadImportFiles {
        std::filesystem::path Directory=std::filesystem::temp_directory_path()/("icax-head-import-"+iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));
        HeadImportFiles(){std::filesystem::create_directory(Directory);}
        ~HeadImportFiles(){for(const auto& name:{"body.stl","body.obj","body.iges","head.urdf","head.sdf","head.i5cam"}){std::error_code error;std::filesystem::remove(Directory/name,error);}std::error_code error;std::filesystem::remove(Directory,error);}
        void Write(const char* name,const Text& text){std::ofstream out(Directory/name,std::ios::binary);out<<text;}
        Map Request(Scene& scene,const char* name){auto result=GenerateRequest(scene);const auto text=(Directory/name).u8string();result["sourcePath"]=Text(text.begin(),text.end());return result;}
        void Meshes(){
            Write("body.stl","solid head\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex .02 0 0\nvertex 0 .03 .04\nendloop\nendfacet\nendsolid head\n");
            Write("body.obj","v 0 0 0\nv .02 0 0\nv 0 .03 .04\nf 1 2 3\n");
        }
    };
}
TEST(FiveAxisHead, StaticGeometryFormatsDeclareNoRotaryJoints)
{
    Scene scene;File stock(BRepPrimAPI_MakeBox(10,20,30).Shape());Imported(scene,stock,"TableTable");HeadImportFiles files;files.Meshes();
    IGESControl_Writer writer;writer.AddShape(BRepPrimAPI_MakeBox(10,15,50).Shape());ASSERT_TRUE(writer.Write((files.Directory/"body.iges").string().c_str()));
    const auto workpiece=Read(scene).at("workpieces");
    auto request=ImportTestHead(scene,stock);ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamHead","Import",request)).at("status").To<Text>());
    EXPECT_EQ("Rigid",Head(scene).at("motionType").To<Text>());EXPECT_EQ(0,Head(scene).at("rotaryAxisCount").To<int>());EXPECT_FALSE(Head(scene).contains("assembly"));
    for(const auto* name:{"body.stl","body.obj","body.iges"}){
        SCOPED_TRACE(name);ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamHead","Import",files.Request(scene,name))).at("status").To<Text>());
        const auto head=Head(scene);EXPECT_EQ("Rigid",head.at("motionType").To<Text>());EXPECT_EQ(0,head.at("rotaryAxisCount").To<int>());EXPECT_FALSE(head.contains("assembly"));EXPECT_FALSE(head.at("calibrated").To<bool>());
        EXPECT_EQ(workpiece,Read(scene).at("workpieces"));
        const auto ref=head.at("geometryRef").To<Map>();const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));ASSERT_TRUE(geometry);EXPECT_FALSE(geometry->Triangulations3.empty());
        if(Text(name)!="body.iges"){EXPECT_TRUE(geometry->Faces.empty());EXPECT_FALSE(head.at("importMetadata").To<Map>().at("validBRep").To<bool>());}
    }
}
TEST(FiveAxisHead, UrdfAndSdfRetainJointsLimitsFramesAndEmbeddedPartsWithoutCreatingAMachine)
{
    Scene scene;File stock(BRepPrimAPI_MakeBox(10,20,30).Shape());Imported(scene,stock);HeadImportFiles files;files.Meshes();
    const auto workpiece=Read(scene).at("workpieces");
    files.Write("head.urdf",R"(<robot name="AB cutting head">
<link name="mount"/><link name="gimbal"><visual><geometry><box size=".08 .09 .1"/></geometry></visual></link>
<link name="tool"><visual><geometry><mesh filename="body.stl" scale="1 2 1"/></geometry></visual><icax_tool name="laser" type="laser_cutting_head" tcp_mm="1 2 -40" beam="0 0 -1"/></link>
<joint name="A" type="revolute"><parent link="mount"/><child link="gimbal"/><origin xyz=".01 .02 .03"/><axis xyz="1 0 0"/><limit lower="-1.5" upper="1.5" effort="10" velocity="2"/></joint>
<joint name="B" type="revolute"><parent link="gimbal"/><child link="tool"/><origin xyz="0 0 -.05"/><axis xyz="0 1 0"/><limit lower="-1.2" upper="1.2" effort="10" velocity="2"/></joint>
</robot>)");
    files.Write("head.sdf",R"(<sdf version="1.7"><model name="AC cutting head"><link name="mount"/>
<link name="gimbal"><visual name="body"><geometry><sphere><radius>.04</radius></sphere></geometry></visual></link>
<link name="tool"><visual name="tool"><geometry><mesh><uri>body.obj</uri></mesh></geometry></visual></link>
<joint name="C" type="revolute"><parent>mount</parent><child>gimbal</child><axis><xyz>0 0 1</xyz><limit><lower>-3</lower><upper>3</upper></limit></axis></joint>
<joint name="A" type="revolute"><parent>gimbal</parent><child>tool</child><axis><xyz>1 0 0</xyz><limit><lower>-1.5</lower><upper>1.5</upper></limit></axis></joint>
</model></sdf>)");
    for(const auto* name:{"head.urdf","head.sdf"}){
        SCOPED_TRACE(name);const auto task=Wait(scene,Invoke(&scene,"CamHead","Import",files.Request(scene,name)));ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
        const auto head=Head(scene);EXPECT_EQ("Articulated",head.at("motionType").To<Text>());EXPECT_EQ(2u,head.at("rotaryAxisCount").To<std::uint64_t>());EXPECT_EQ("Imported",head.at("assembly").To<Map>().at("source").To<Text>());
        const auto assembly=head.at("assembly").To<Map>();const auto nodes=assembly.at("nodes").To<Array>();ASSERT_EQ(Text(name)=="head.urdf"?3u:4u,nodes.size());EXPECT_TRUE(assembly.at("workpieceAxes").To<Array>().empty());EXPECT_EQ(workpiece,Read(scene).at("workpieces"));
        EXPECT_EQ("fixed",nodes[0].To<Map>().at("type").To<Text>());EXPECT_EQ(2,std::count_if(nodes.begin(),nodes.end(),[](const auto& n){return n.To<Map>().at("type").To<Text>()=="revolute";}));
        EXPECT_TRUE(scene.Database().FilterEntities([](auto e){return e->HasComponent("CMachineInstanceComponent");}).empty());
        if(Text(name)=="head.urdf"){EXPECT_EQ((Array{10.,20.,30.}),Array({nodes[1].To<Map>().at("origin").To<Array>()[3],nodes[1].To<Map>().at("origin").To<Array>()[7],nodes[1].To<Map>().at("origin").To<Array>()[11]}));EXPECT_EQ((Array{1.,2.,-40.}),head.at("tcpLocalPosition").To<Array>());EXPECT_TRUE(head.at("calibrated").To<bool>());}
        const auto a=std::find_if(nodes.begin(),nodes.end(),[](const auto& n){return n.To<Map>().at("joint").To<Text>()=="A";});ASSERT_NE(nodes.end(),a);EXPECT_EQ(-1.5,a->To<Map>().at("lower").To<double>());
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Imported cutting head";file.Save(files.Directory/"head.i5cam",info,scene.Database(),scene.Resources());
        std::filesystem::remove(files.Directory/name);std::filesystem::remove(files.Directory/(Text(name)=="head.urdf"?"body.stl":"body.obj"));
        Scene reopened(scene.Project,scene.GetSceneID(),false,true);file.Open(files.Directory/"head.i5cam",reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(head,Head(reopened));
        for(const auto& value:Head(reopened).at("assembly").To<Map>().at("nodes").To<Array>()){const auto ref=value.To<Map>().at("previewRef").To<Map>();if(ref.empty())continue;EXPECT_TRUE(reopened.Resources().Get<iCAX::Resource::CFlatBufferResource>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>())));}
        ASSERT_TRUE(scene.Database().Undo());ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(head,Head(scene));
    }
}
TEST(FiveAxisHead, FullMachineAndAmbiguousToolDefinitionsAreRejectedWithoutReplacingTheHead)
{
    Scene scene;File stock(BRepPrimAPI_MakeBox(10,20,30).Shape());Imported(scene,stock);HeadImportFiles files;const auto before=Head(scene);
    files.Write("head.urdf",R"(<robot name="invalid"><link name="base"/><link name="tool"/><joint name="X" type="prismatic"><parent link="base"/><child link="tool"/><axis xyz="1 0 0"/><limit lower="0" upper="1" effort="10" velocity="1"/></joint></robot>)");
    const auto failed=Wait(scene,Invoke(&scene,"CamHead","Import",files.Request(scene,"head.urdf")));EXPECT_EQ("Failed",failed.at("status").To<Text>());EXPECT_NE(Text::npos,failed.at("error").To<Text>().find("CAM_HEAD_ROTARY_JOINTS_ONLY"));EXPECT_EQ(before,Head(scene));
    files.Write("head.urdf",R"(<robot name="ambiguous"><link name="base"/><link name="tool1"/><link name="tool2"/><joint name="one" type="fixed"><parent link="base"/><child link="tool1"/></joint><joint name="two" type="fixed"><parent link="base"/><child link="tool2"/></joint></robot>)");
    const auto ambiguous=Wait(scene,Invoke(&scene,"CamHead","Import",files.Request(scene,"head.urdf")));EXPECT_EQ("Failed",ambiguous.at("status").To<Text>());EXPECT_NE(Text::npos,ambiguous.at("error").To<Text>().find("CAM_HEAD_TOOL_AMBIGUOUS"));EXPECT_EQ(before,Head(scene));
}
