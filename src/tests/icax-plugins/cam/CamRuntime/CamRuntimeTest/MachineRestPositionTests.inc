namespace machine_rest_position_tests {
    using ID=iCAX::Data::uuid;
    struct Fixture {
        CTestApplicationContext Application;CTestProductContext Product;CTestProjectContext Project;CTestSceneContext Scene;
        std::shared_ptr<iCAX::Behaviour::CBehaviourBase> Pose;
        ID Machine=iCAX::Data::GenerateNewUUID(),Base=iCAX::Data::GenerateNewUUID(),Axis=iCAX::Data::GenerateNewUUID(),Tool=iCAX::Data::GenerateNewUUID(),Mounted=iCAX::Data::GenerateNewUUID();
        explicit Fixture(bool rotary=false){
            auto root=Scene.Database().CreateEntity(Machine);root->AddComponent<iCAX::CAM::CMachineInstanceComponent>();root->AddComponent<iCAX::Transform::CTransformComponent>();
            Part(Base,Machine,{10.,0.,0.,0.,0.,0.});const auto moving=Part(Axis,Base,{0.,20.,0.,.2,-.1,.3});
            const auto joint=moving->AddComponent<iCAX::CAM::CMachineJointComponent>();std::string error;
            if(!joint->SetProperties({{"JointName",std::string("Motion")},{"JointType",std::string(rotary?"revolute":"prismatic")},{"Axis",VariantArray{0.,0.,1.}},
                {"LowerLimit",-100.},{"UpperLimit",100.},{"HomePosition",2.},{"VelocityLimit",25.},{"Position",rotary?.4:5.}},error))throw std::runtime_error(error);
            Part(Tool,Axis,{2.,3.,4.,.1,.2,.3});
            auto work=Scene.Database().CreateEntity(Mounted);auto transform=work->AddComponent<iCAX::Transform::CTransformComponent>();
            if(!transform->SetProperties({{"PositionX",6.},{"PositionY",7.},{"PositionZ",8.}},error) || !iCAX::Transform::SetTransformParent(Scene.Database(),Mounted,Tool,error))throw std::runtime_error(error);
            for(const auto id:{Base,Axis,Tool})iCAX::CAM::SynchronizeMachineElementPose(*Scene.Database().GetEntity(id));
            const auto registry=iCAX::Behaviour::CreateBehaviourRegistry();iCAX::Behaviour::CBehaviourRegistrationCatalog::ReplayAll(*registry);Pose=registry->CreateBehaviourByType(std::string("CMachinePoseBehaviour"));if(!Pose)throw std::runtime_error("Registered machine pose behaviour absent");Frame();
        }
        std::shared_ptr<iCAX::Database::IEntity> Part(ID id,ID parent,VariantArray rest){
            auto entity=Scene.Database().CreateEntity(id);entity->AddComponent<iCAX::Transform::CTransformComponent>();const auto element=entity->AddComponent<iCAX::CAM::CMachineElementComponent>();std::string error;
            if(!element->SetProperties({{"MachineID",Machine},{"ElementKind",std::string("part")},{"Name",iCAX::Data::to_string(id)},{"RestPose",rest}},error) || !iCAX::Transform::SetTransformParent(Scene.Database(),id,parent,error))throw std::runtime_error(error);return entity;
        }
        ObjectMap Edit(ObjectMap request){return DecodeResponseObject(iCAX::CAM::SDO::HandleSetMachineElementPosition(MakeSDOCall("Machine","SetElementPosition",request),Application,nullptr,nullptr,&Scene));}
        ObjectMap Read(ID id){return DecodeResponseObject(iCAX::CAM::SDO::HandleGetMachineElement(MakeSDOCall("Machine","GetElement",{{"entityId",iCAX::Data::to_string(id)}}),Application,nullptr,nullptr,&Scene)).at("machineElement").To<ObjectMap>();}
        auto World(ID id){return Scene.Database().GetEntity(id)->GetComponent<iCAX::Transform::CTransformComponent>()->GetLocalToWorldMatrix();}
        auto Rest(ID id){return Scene.Database().GetEntity(id)->GetComponent<iCAX::CAM::CMachineElementComponent>()->GetRestPose();}
        // Transform is a non-persistent projection. A real host refreshes it
        // from the restored RestPose through this registered behaviour each frame.
        void Frame(){for(const auto id:{Base,Axis,Tool})Pose->Update(*Scene.Database().GetEntity(id)->GetComponent<iCAX::CAM::CMachineElementComponent>(),Application,Product,Project,Scene,0.,0.);}
    };
    void WorldShift(const auto& before,const auto& after,const std::array<double,3>& delta){for(int r=0;r<4;++r)for(int c=0;c<4;++c)EXPECT_NEAR(before(r,c)+(r<3&&c==3?delta[r]:0.),after(r,c),1e-8);}
}

TEST(CamRuntimeMachineRestPosition, NonzeroJointPoseRetainsMechanismAndMovesMountedDescendantsWithSingleUndo)
{
    using namespace machine_rest_position_tests;
    for(const auto rotary:{false,true}){
        SCOPED_TRACE(rotary?"rotary":"linear");Fixture f(rotary);const auto before=f.Read(f.Axis);const auto originalRest=f.Rest(f.Axis);const auto axis=f.World(f.Axis),tool=f.World(f.Tool),work=f.World(f.Mounted);const auto history=f.Scene.Database().GetUndoArray().size();
        const auto result=f.Edit({{"entityId",iCAX::Data::to_string(f.Axis)},{"position",VariantArray{100,200,300}}});ASSERT_TRUE(result.at("changed").To<bool>());EXPECT_EQ(history+1,f.Scene.Database().GetUndoArray().size());
        const auto after=f.Read(f.Axis);EXPECT_EQ((VariantArray{100.,200.,300.}),after.at("restPosition").To<VariantArray>());EXPECT_FALSE(after.contains("restPose"));EXPECT_EQ(before.at("joint"),after.at("joint"));EXPECT_EQ(before.at("parentEntityId"),after.at("parentEntityId"));EXPECT_EQ(before.at("childEntityIds"),after.at("childEntityIds"));
        const auto rest=f.Rest(f.Axis);for(int i=3;i<6;++i)EXPECT_EQ(originalRest[i],rest[i]);WorldShift(axis,f.World(f.Axis),{100,180,300});WorldShift(tool,f.World(f.Tool),{100,180,300});WorldShift(work,f.World(f.Mounted),{100,180,300});
        ASSERT_TRUE(f.Scene.Database().Undo());EXPECT_EQ(originalRest,f.Rest(f.Axis));f.Frame();WorldShift(axis,f.World(f.Axis),{0,0,0});WorldShift(work,f.World(f.Mounted),{0,0,0});ASSERT_TRUE(f.Scene.Database().Redo());EXPECT_EQ(rest,f.Rest(f.Axis));f.Frame();WorldShift(work,f.World(f.Mounted),{100,180,300});
        const auto listed=result.at("machine").To<ObjectMap>().at("elements").To<VariantArray>();bool found=false;for(const auto& value:listed){const auto e=value.To<ObjectMap>();if(e.at("entityId")==Variant(iCAX::Data::to_string(f.Axis))){found=true;EXPECT_EQ(after.at("restPosition"),e.at("restPosition"));}}EXPECT_TRUE(found);
    }
}

TEST(CamRuntimeMachineRestPosition, FixedPartTranslationAndIdenticalSubmissionPreserveJointAndHistory)
{
    using namespace machine_rest_position_tests;Fixture f;const auto before=f.Read(f.Axis);const auto work=f.World(f.Mounted);const auto root=f.World(f.Machine);const auto history=f.Scene.Database().GetUndoArray().size();
    const ObjectMap request{{"entityId",iCAX::Data::to_string(f.Base)},{"position",VariantArray{15.,-7.,11.}}};ASSERT_TRUE(f.Edit(request).at("changed").To<bool>());WorldShift(work,f.World(f.Mounted),{5,-7,11});EXPECT_EQ(before.at("joint"),f.Read(f.Axis).at("joint"));WorldShift(root,f.World(f.Machine),{0,0,0});
    EXPECT_FALSE(f.Edit(request).at("changed").To<bool>());EXPECT_EQ(history+1,f.Scene.Database().GetUndoArray().size());ASSERT_TRUE(f.Scene.Database().Undo());f.Frame();WorldShift(work,f.World(f.Mounted),{0,0,0});ASSERT_TRUE(f.Scene.Database().Redo());f.Frame();WorldShift(work,f.World(f.Mounted),{5,-7,11});
}

TEST(CamRuntimeMachineRestPosition, InvalidCoordinatesForeignFieldsAndWrongEntitiesNeverPartiallyMutate)
{
    using namespace machine_rest_position_tests;Fixture f;const auto before=f.Read(f.Axis);const auto rest=f.Rest(f.Axis);const auto work=f.World(f.Mounted);const auto history=f.Scene.Database().GetUndoArray().size();
    const ObjectMap valid{{"entityId",iCAX::Data::to_string(f.Axis)},{"position",VariantArray{100.,200.,300.}}};std::vector<ObjectMap> invalid;
    for(const auto& position:std::vector<Variant>{VariantArray{1.,2.},VariantArray{1.,2.,3.,4.},VariantArray{1.,true,3.},VariantArray{1.,std::string("2"),3.},VariantArray{1.,std::numeric_limits<double>::infinity(),3.},VariantArray{1.,std::numeric_limits<double>::quiet_NaN(),3.},std::string("1,2,3")}){auto r=valid;r["position"]=position;invalid.push_back(r);}
    for(const auto field:{"axis","rotation","rotationRadians","restPose","homePosition","lower","upper","velocityLimit","positionX"}){auto r=valid;r[field]=0.;invalid.push_back(r);}
    for(const auto& id:{iCAX::Data::to_string(f.Machine),iCAX::Data::to_string(f.Mounted),iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()),std::string("bad-id")}){auto r=valid;r["entityId"]=id;invalid.push_back(r);}invalid.push_back({{"entityId",iCAX::Data::to_string(f.Axis)}});invalid.push_back({{"position",VariantArray{1.,2.,3.}}});
    for(const auto& request:invalid){EXPECT_THROW(f.Edit(request),std::invalid_argument);EXPECT_EQ(rest,f.Rest(f.Axis));EXPECT_EQ(before,f.Read(f.Axis));EXPECT_EQ(history,f.Scene.Database().GetUndoArray().size());WorldShift(work,f.World(f.Mounted),{0,0,0});}
}

TEST(CamRuntimeMachineRestPosition, RepositoryRejectionKeepsPersistentOriginDerivedPoseAndUndoHistory)
{
    using namespace machine_rest_position_tests;Fixture f;struct Reject final:iCAX::Database::IChecker{
        bool AllowAttachByName(const iCAX::Database::IEntity&,const std::string&,std::string&)override{return true;}bool AllowRemoveByName(const iCAX::Database::IEntity&,const std::string&,std::string&)override{return true;}
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet& fields,std::string& error)override{if(fields.contains("RestPose")){error="Native test rejects dimension mutation";return false;}return true;}
    };f.Scene.Database().GetMetaRegistry()->RegistCheckerByName(std::make_shared<Reject>(),iCAX::CAM::CMachineElementComponent::S_ClassName);
    const auto before=f.Read(f.Axis);const auto rest=f.Rest(f.Axis);const auto work=f.World(f.Mounted);const auto history=f.Scene.Database().GetUndoArray().size();EXPECT_THROW(f.Edit({{"entityId",iCAX::Data::to_string(f.Axis)},{"position",VariantArray{777.,2.,3.}}}),std::invalid_argument);EXPECT_EQ(rest,f.Rest(f.Axis));EXPECT_EQ(before,f.Read(f.Axis));EXPECT_EQ(history,f.Scene.Database().GetUndoArray().size());WorldShift(work,f.World(f.Mounted),{0,0,0});
}
