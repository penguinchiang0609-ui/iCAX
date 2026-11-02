namespace {
    Array UnitMatrixProduct(const Array& left,const Array& right,bool inverseRight=false) {
        const auto a=RigidMatrix(left),b=RigidMatrix(right);std::array<double,16> r=b;
        if(inverseRight){for(int i=0;i<3;++i){for(int j=0;j<3;++j)r[i*4+j]=b[j*4+i];r[i*4+3]=0;for(int j=0;j<3;++j)r[i*4+3]-=b[j*4+i]*b[j*4+3];}}
        Array out;for(int i=0;i<4;++i)for(int j=0;j<4;++j){double v=0;for(int k=0;k<4;++k)v+=a[i*4+k]*r[k*4+j];out.emplace_back(v);}return out;
    }
    template<class F>Map FixtureUnitCommand(Scene& scene,const Map& request,const Text& method,F fn){
        auto runtime=Runtime(scene);const auto command=String(request,"commandId"),payload=method+iCAX::Data::VariantSerializer::Serialize(request);(void)Identifier(request,"commandId");
        if(const auto it=runtime->Receipts.find(command);it!=runtime->Receipts.end()){if(it->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return it->second.second;}
        CheckExpected(*runtime,request);CheckCapacity(*runtime);
        const auto id=Identifier(request,"unitId"),setupID=Identifier(request,"setupId");const auto unit=Require<CFiveAxisFixtureUnitComponent>(scene.Database(),id);
        if(unit->GetSetupID()!=setupID)throw std::invalid_argument("CAM_SETUP_MISMATCH");
        std::vector<ID> members;for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))if(const auto f=Component<CFiveAxisFixtureComponent>(e);f&&f->GetUnitID()==id)members.push_back(e->GetID());
        fn(id,setupID,unit,members);Map result{{"unitId",request.at("unitId")}};runtime->Receipts.emplace(command,std::make_pair(payload,result));PumpTasks(scene);return result;
    }
}
Map UpdateFixtureUnit(Scene& scene,const Map& request){
    return FixtureUnitCommand(scene,request,"UnitUpdate",[&](const ID& id,const ID& setupID,const auto& unit,const auto& members){
        if(unit->GetKind()=="Generated"&&request.contains("transform"))throw std::invalid_argument("CAM_FIXTURE_GENERATED_TRANSFORM_FIXED: 生成支撑的拼装位置固定，不能平移或旋转。");
        if(unit->GetLocked()&&(request.contains("name")||request.contains("transform")))throw std::invalid_argument("CAM_FIXTURE_LOCKED: 请先解锁夹具单元。");
        iCAX::Data::PropertySet changes;Array delta;bool invalidate=false;
        if(request.contains("name")){const auto name=String(request,"name");if(name.empty()||name.size()>512)throw std::invalid_argument("CAM_FIXTURE_NAME_INVALID");changes["Name"]=name;}
        if(request.contains("transform")){const auto matrix=request.at("transform").To<Array>();RigidMatrix(matrix);delta=UnitMatrixProduct(matrix,unit->GetTransform(),true);changes["Transform"]=matrix;invalidate=matrix!=unit->GetTransform();}
        if(request.contains("locked"))changes["Locked"]=request.at("locked").To<bool>();
        if(request.contains("enabled")){changes["Enabled"]=request.at("enabled").To<bool>();invalidate=true;}
        if(changes.empty())throw std::invalid_argument("CAM_FIXTURE_UPDATE_EMPTY");changes["InputRevision"]=Next(unit->GetInputRevision());
        Transaction(scene.Database(),"修改夹具单元",[&](auto& tx){
            tx.ModifyComponent(id,CFiveAxisFixtureUnitComponent::S_ClassName,changes);
            for(const auto& part:members){const auto f=Require<CFiveAxisFixtureComponent>(scene.Database(),part);iCAX::Data::PropertySet child;
                if(!delta.empty())child["Transform"]=UnitMatrixProduct(delta,f->GetTransform());
                if(request.contains("locked"))child["Locked"]=request.at("locked").To<bool>();
                if(request.contains("enabled")){bool enabled=request.at("enabled").To<bool>();const auto design=unit->GetDesign();if(unit->GetKind()=="Generated"&&design.contains("assembly"))for(const auto& value:design.at("assembly").To<Map>().at("parts").To<Array>()){const auto p=value.To<Map>();if(Identifier(p,"partId")==part&&p.contains("enabled"))enabled&=p.at("enabled").To<bool>();}child["Enabled"]=enabled;}
                if(!child.empty()){child["InputRevision"]=Next(f->GetInputRevision());tx.ModifyComponent(part,CFiveAxisFixtureComponent::S_ClassName,child);}
            }
            if(invalidate)Invalidate(tx,scene.Database(),setupID);
        });
    });
}
Map RemoveFixtureUnit(Scene& scene,const Map& request){
    return FixtureUnitCommand(scene,request,"UnitRemove",[&](const ID& id,const ID& setupID,const auto& unit,const auto& members){
        if(unit->GetLocked())throw std::invalid_argument("CAM_FIXTURE_LOCKED: 请先解锁夹具单元。");
        Transaction(scene.Database(),"删除夹具单元",[&](auto& tx){for(const auto& part:members)tx.DisposeEntity(part);tx.DisposeEntity(id);Invalidate(tx,scene.Database(),setupID);});
    });
}
