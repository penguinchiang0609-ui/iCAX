namespace
{
    Map OrderOptions(const Map& request)
    {
        Map result{{"pattern",Text("Smart")},{"objective",Text("Distance")},{"scope",Text("All")},{"processScope",Text("Part")},
            {"plane",Text("XY")},{"normal",Array{0.,0.,1.}},{"rowAngleDeg",0.},{"corner",Text("BottomLeft")},
            {"inward",true},{"clockwise",true},{"skip",0.},{"start",Text("Auto")},{"heightPenaltyWeight",3.},{"optimizeStarts",false}};
        if(request.contains("options")){
            const auto options=request.at("options").To<Map>();CutFields(options,{"pattern","objective","scope","processScope","plane","normal","rowAngleDeg","corner","inward","clockwise","skip","start","heightPenaltyWeight","optimizeStarts"});
            for(const auto& [key,value]:options)result[key]=value;
        }
        const auto choice=[&](const char* key,std::initializer_list<const char*> values){const auto value=String(result,key);for(const auto* candidate:values)if(value==candidate)return;throw std::invalid_argument("CAM_ORDER_OPTION_INVALID: "+Text(key));};
        choice("pattern",{"Smart","Z","Snake","Ring"});choice("objective",{"Time","Distance"});choice("scope",{"All","Selected","From"});
        choice("processScope",{"Part","Setup"});choice("plane",{"XY","XZ","YZ","Custom"});choice("corner",{"BottomLeft","BottomRight","TopLeft","TopRight"});choice("start",{"Auto","Origin","Current"});
        for(const auto* key:{"inward","clockwise","optimizeStarts"})if(!result.at(key).Is<bool>())throw std::invalid_argument("CAM_ORDER_OPTION_INVALID");
        // Existing saved options cannot enable geometry edits during sorting.
        result["optimizeStarts"]=false;
        const auto heightWeight=RegistrationNumber(result.at("heightPenaltyWeight"));
        if(!std::isfinite(heightWeight))throw std::invalid_argument("CAM_ORDER_HEIGHT_PENALTY_INVALID");
        const auto skip=RegistrationNumber(result.at("skip")),angle=RegistrationNumber(result.at("rowAngleDeg"));
        if(skip<0||skip>16||std::floor(skip)!=skip||!std::isfinite(angle)||std::abs(angle)>360)throw std::invalid_argument("CAM_ORDER_OPTION_INVALID");
        const auto normal=RegistrationPoint(result.at("normal"));if(Ordering::Distance(normal,{})<1e-9)throw std::invalid_argument("CAM_ORDER_PLANE_INVALID");
        return result;
    }
    void ConfigureOrderSolver(Ordering::Solver& solver,const Map& request,const Map& options)
    {
        solver.CombineMarkAndHole=OrderStrategy(request)=="MarkAndHoleTrim";
        solver.GlobalProcess=String(options,"processScope")=="Setup";solver.Pattern=String(options,"pattern");solver.DistanceObjective=String(options,"objective")=="Distance";
        solver.HeightPenaltyWeight=RegistrationNumber(options.at("heightPenaltyWeight"));
        solver.Skip=static_cast<unsigned>(RegistrationNumber(options.at("skip")));solver.Bottom=String(options,"corner").starts_with("Bottom");solver.Right=String(options,"corner").ends_with("Right");
        solver.Inward=options.at("inward").To<bool>();solver.Clockwise=options.at("clockwise").To<bool>();solver.UseOrigin=String(options,"start")!="Auto";
        const auto plane=String(options,"plane");std::array<double,3> x{1,0,0},y{0,1,0};
        if(plane=="XZ")y={0,0,1};else if(plane=="YZ"){x={0,1,0};y={0,0,1};}
        else if(plane=="Custom"){
            auto z=RegistrationPoint(options.at("normal"));const auto length=Ordering::Distance(z,{});for(auto& v:z)v/=length;
            x=std::abs(z[0])<.9?std::array<double,3>{1,0,0}:std::array<double,3>{0,1,0};double dot=0;for(int i=0;i<3;++i)dot+=x[i]*z[i];for(int i=0;i<3;++i)x[i]-=dot*z[i];
            const auto nx=Ordering::Distance(x,{});for(auto& v:x)v/=nx;y={z[1]*x[2]-z[2]*x[1],z[2]*x[0]-z[0]*x[2],z[0]*x[1]-z[1]*x[0]};}
        const auto angle=RegistrationNumber(options.at("rowAngleDeg"))*std::numbers::pi/180.;
        for(int i=0;i<3;++i){solver.PlaneX[i]=std::cos(angle)*x[i]+std::sin(angle)*y[i];solver.PlaneY[i]=-std::sin(angle)*x[i]+std::cos(angle)*y[i];}
        const auto scope=String(options,"scope");if(scope=="All")return;
        std::set<Text> editable;
        if(scope=="Selected"){
            if(!request.contains("intentIds"))throw std::invalid_argument("CAM_ORDER_SELECTION_REQUIRED");
            for(const auto& id:request.at("intentIds").To<Array>())if(!editable.insert(id.To<Text>()).second)throw std::invalid_argument("CAM_ORDER_SELECTION_INVALID");
        }else{
            const auto id=String(request,"fromIntentId");bool found=false;for(const auto& cut:solver.Cuts){found|=cut.ID==id;if(found)editable.insert(cut.ID);}
            if(!found)throw std::invalid_argument("CAM_ORDER_SELECTION_REQUIRED");
        }
        if(editable.empty())throw std::invalid_argument("CAM_ORDER_SELECTION_REQUIRED");
        for(auto& cut:solver.Cuts)cut.Sortable=editable.contains(cut.ID);
        for(const auto& id:editable)if(std::none_of(solver.Cuts.begin(),solver.Cuts.end(),[&](const auto& cut){return cut.ID==id;}))throw std::invalid_argument("CAM_ORDER_SELECTION_INVALID");
        for(auto& node:solver.Nodes){const bool frozen=std::any_of(node.Leaves.begin(),node.Leaves.end(),[&](auto i){return !editable.contains(solver.Cuts[i].ID);});
            if(frozen)node.Anchored=true;if(frozen&&node.Children.empty())node.Locked=true;}
    }
}
