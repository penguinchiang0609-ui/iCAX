// Drawing workspaces are projections and commands over the shared geometry.
// No workspace DTO is persisted as a second authoring model.
namespace TubeCadWorkspace {
namespace T=iCAX::GeometryData::Tube;
namespace G=iCAX::GeometryData;
constexpr double Tau=6.28318530717958647692;
VariantArray Array(const ObjectMap& object,const std::string& key) {
    const auto it=object.find(key);if(it==object.end()||!it->second.Is<VariantArray>())
        throw std::invalid_argument("CAD 工作区缺少数组字段: "+key);return it->second.To<VariantArray>();
}
// JSON has one numeric domain. An unchanged workspace value must remain
// unchanged after integer-looking doubles travel through the browser.
bool SameValue(const Variant& a,const Variant& b) {
    const auto numeric=[](const Variant& v){return std::visit([](const auto& n){using N=std::decay_t<decltype(n)>;
        return std::is_arithmetic_v<N>&&!std::is_same_v<N,bool>;},v.m_Value);};
    if(numeric(a)&&numeric(b))return ToDouble(a,"workspace value")==ToDouble(b,"workspace value");
    if(a.Is<ObjectMap>()&&b.Is<ObjectMap>()) {
        const auto left=a.To<ObjectMap>(),right=b.To<ObjectMap>();if(left.size()!=right.size())return false;
        for(const auto& [key,value]:left){const auto it=right.find(key);if(it==right.end()||!SameValue(value,it->second))return false;}return true;
    }
    if(a.Is<VariantArray>()&&b.Is<VariantArray>()) {
        const auto left=a.To<VariantArray>(),right=b.To<VariantArray>();if(left.size()!=right.size())return false;
        for(std::size_t i=0;i<left.size();++i)if(!SameValue(left[i],right[i]))return false;return true;
    }
    return a==b;
}

const T::SSolidNode& Node(const T::CTubeCadDocument& doc,const std::string& id) {
    const auto it=std::find_if(doc.Construction.SolidNodes.begin(),doc.Construction.SolidNodes.end(),[&](const auto& n){return n.ID==id;});
    if(it==doc.Construction.SolidNodes.end())throw std::invalid_argument("CAD 工作区构造已改变，请重新读取");
    return *it;
}
const T::SExtrudedRegionNode& Stock(const T::CTubeCadDocument& doc) {
    const auto* stock=std::get_if<T::SExtrudedRegionNode>(&Node(doc,doc.Construction.BaseNodeID).Data);
    if(!stock||stock->Section.Boundaries.empty())throw std::invalid_argument("CAD 工作区需要明确的直管原材支撑");
    return *stock;
}
ObjectMap Segment(const G::CurveSegment2& trimmed) {
    const auto pair=[](const G::Point2& p){return VariantArray{p.X,p.Y};};
    return std::visit([&](const auto& c)->ObjectMap {
        using C=std::decay_t<decltype(c)>;
        if constexpr(std::is_same_v<C,G::Segment2>) {
            auto a=c.Start,b=c.End;if(trimmed.Range.Reversed)std::swap(a,b);
            return {{"kind",std::string("line")},{"x1",a.X},{"y1",a.Y},{"x2",b.X},{"y2",b.Y}};
        } else if constexpr(std::is_same_v<C,G::Circle2>||std::is_same_v<C,G::Ellipse2>) {
            const auto& p=c.Placement;const double hand=p.XDirection.X*p.YDirection.Y-p.XDirection.Y*p.YDirection.X;
            if(std::abs(std::abs(hand)-1)>1e-7)throw std::invalid_argument("CAD 曲线基向量不是正交单位坐标系");
            double first=trimmed.Range.First,last=trimmed.Range.Last;
            if(trimmed.Range.Reversed)std::swap(first,last);
            ObjectMap s{{"kind",std::string(std::is_same_v<C,G::Circle2>?"circleArc":"ellipseArc")},
                {"cx",p.Location.X},{"cy",p.Location.Y},{"rotation",std::atan2(p.XDirection.Y,p.XDirection.X)},
                {"startAngle",hand*first},{"sweep",hand*(last-first)}};
            if constexpr(std::is_same_v<C,G::Circle2>)s["radius"]=c.Radius;
            else {s["radiusX"]=c.MajorRadius;s["radiusY"]=c.MinorRadius;}
            return s;
        } else if constexpr(std::is_same_v<C,G::Bezier2>) {
            auto poles=c.Poles;
            const auto split=[](std::vector<G::Point2> p,double t) {
                std::vector<G::Point2> a{p.front()},b{p.back()};
                while(p.size()>1) {for(std::size_t i=0;i+1<p.size();++i)p[i]={p[i].X+(p[i+1].X-p[i].X)*t,p[i].Y+(p[i+1].Y-p[i].Y)*t};
                    p.pop_back();a.push_back(p.front());b.push_back(p.back());}
                std::reverse(b.begin(),b.end());return std::pair{a,b};
            };
            if(trimmed.Range.Last<1)poles=split(poles,trimmed.Range.Last).first;
            if(trimmed.Range.First>0)poles=split(poles,trimmed.Range.First/trimmed.Range.Last).second;
            if(trimmed.Range.Reversed)std::reverse(poles.begin(),poles.end());
            VariantArray points;for(const auto& p:poles)points.emplace_back(pair(p));
            return {{"kind",std::string("bezier")},{"points",points}};
        } else throw std::invalid_argument("此精确曲线尚无当前绘图页面的可编辑投影");
    },trimmed.Curve);
}
ObjectMap Entity(const T::SRegionLoop2& loop,const std::string& id) {
    // The existing ellipse workspace edits the complete primitive's axes.
    // Only one complete exact curve has that identity; multiple trimmed
    // curves remain a path so their range and segment selections survive.
    if(loop.Curves.size()==1) {
        const auto& trimmed=loop.Curves.front().Segment;
        if(const auto* ellipse=std::get_if<G::Ellipse2>(&trimmed.Curve);
            ellipse&&std::abs(trimmed.Range.Last-trimmed.Range.First-Tau)<1e-7) {
            const auto projected=Segment(trimmed);
            return {{"id",id},{"kind",std::string("ellipse")},{"closed",true},
                {"cx",projected.at("cx")},{"cy",projected.at("cy")},
                {"radiusX",ellipse->MajorRadius},{"radiusY",ellipse->MinorRadius},
                {"rotation",projected.at("rotation")}};
        }
    }
    if(!loop.Curves.empty())if(const auto* circle=std::get_if<G::Circle2>(&loop.Curves.front().Segment.Curve)) {
        double sum=0;bool full=true;
        for(const auto& edge:loop.Curves) {const auto* part=std::get_if<G::Circle2>(&edge.Segment.Curve);
            if(!part||std::hypot(part->Placement.Location.X-circle->Placement.Location.X,part->Placement.Location.Y-circle->Placement.Location.Y)>1e-7
                ||std::abs(part->Radius-circle->Radius)>1e-7){full=false;break;}
            sum+=edge.Segment.Range.Last-edge.Segment.Range.First;
        }
        if(full&&std::abs(sum-Tau)<1e-7)return {{"id",id},{"kind",std::string("circle")},{"cx",circle->Placement.Location.X},{"cy",circle->Placement.Location.Y},{"radius",circle->Radius},{"rotation",0.0}};
    }
    VariantArray segments;for(const auto& c:loop.Curves)segments.emplace_back(Segment(c.Segment));
    return {{"id",id},{"kind",std::string("path")},{"closed",true},{"segments",segments}};
}
ObjectMap Profile(const T::CPlanarRegion2& region) {
    VariantArray contours;for(const auto& loop:region.Boundaries) {
        const auto entity=Entity(loop,loop.ID);
        VariantArray segments;
        if(entity.contains("segments"))segments=entity.at("segments").To<VariantArray>();
        else for(const auto& edge:loop.Curves)segments.emplace_back(Segment(edge.Segment));
        contours.emplace_back(ObjectMap{{"kind",std::string("path")},{"closed",true},{"segments",segments}});
    }
    return {{"kind",std::string("profile-package")},{"name",std::string("CAD 精确截面")},
        {"contours",contours},{"editableParameters",false},{"frozenGeometry",false}};
}
G::BRepModel BRep(const TopoDS_Shape& shape) {
    return iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"","",.025);
}
TopoDS_Shape Native(const G::BRepModel& body) {
    const auto shape=iCAX::OpenCascade::BuildOpenCascadeShape(body);
    if(!shape.bOK||shape.Shape.IsNull())throw std::invalid_argument("CAD 工作区实体无法恢复");return shape.Shape;
}
void Remove(T::CTubeCadDocument& doc,const std::string& id) {
    if(id==doc.Construction.BaseNodeID)throw std::invalid_argument("不能删除 CAD 原材");
    const auto& found=Node(doc,id);
    (void)found;
    for(auto& n:doc.Construction.SolidNodes) {
        if(n.ID==id)continue;
        if(auto* b=std::get_if<T::SBooleanNode>(&n.Data))std::erase(b->Children,id);
        if(n.Relations&&std::find(n.Relations->DependsOnNodeIDs.begin(),n.Relations->DependsOnNodeIDs.end(),id)!=n.Relations->DependsOnNodeIDs.end())
            throw std::invalid_argument("构造仍被其他 CAD 特征引用，请先处理依赖");
    }
    std::erase_if(doc.Construction.SolidNodes,[&](const auto& n){return n.ID==id||(n.Metadata.contains("constructionOwner")&&n.Metadata.at("constructionOwner")==id);});
    if(doc.Construction.RootNodeID==id)throw std::invalid_argument("不能删除整个 CAD 结果");
}
void AppendMaterialOperand(T::CTubeCadDocument& doc,const std::string& id,T::EBooleanOperation operation) {
    // A piece selects from the complete, editable source construction. New
    // material operations must enter that construction before selection.
    std::string selectionId,rootId=doc.Construction.RootNodeID;
    for(auto current=rootId;;) {
        const auto& node=Node(doc,current);
        if(const auto* component=std::get_if<T::SConnectedComponentNode>(&node.Data)) {
            selectionId=current;rootId=component->Child;break;
        }
        if(const auto* modifier=std::get_if<T::STopologyBevelNode>(&node.Data)){current=modifier->Child;continue;}
        break;
    }
    auto& root=*std::find_if(doc.Construction.SolidNodes.begin(),doc.Construction.SolidNodes.end(),[&](const auto& n){return n.ID==rootId;});
    if(auto* b=std::get_if<T::SBooleanNode>(&root.Data);b&&b->Operation==operation)b->Children.push_back(id);
    else {
        const auto newId="cad-result/"+UuidToString(iCAX::Data::GenerateNewUUID());
        doc.Construction.SolidNodes.push_back({newId,"零件材料区域",T::SBooleanNode{operation,{rootId,id}}});
        if(selectionId.empty())doc.Construction.RootNodeID=newId;
        else std::get<T::SConnectedComponentNode>(std::find_if(doc.Construction.SolidNodes.begin(),doc.Construction.SolidNodes.end(),[&](const auto& n){return n.ID==selectionId;})->Data).Child=newId;
    }
}
void AddOperand(T::CTubeCadDocument& doc,T::SSolidNode node,T::EBooleanOperation operation=T::EBooleanOperation::Difference) {
    const auto id=node.ID;doc.Construction.SolidNodes.push_back(std::move(node));AppendMaterialOperand(doc,id,operation);
}
bool NearCurveValue(const Variant& a,const Variant& b,const std::string& key={}) {
    if(a.Is<ObjectMap>()&&b.Is<ObjectMap>()) {
        const auto x=a.To<ObjectMap>(),y=b.To<ObjectMap>();if(x.size()!=y.size())return false;
        for(const auto& [name,value]:x)if(!y.contains(name)||!NearCurveValue(value,y.at(name),name))return false;return true;
    }
    if(a.Is<VariantArray>()&&b.Is<VariantArray>()) {
        const auto x=a.To<VariantArray>(),y=b.To<VariantArray>();if(x.size()!=y.size())return false;
        for(std::size_t i=0;i<x.size();++i)if(!NearCurveValue(x[i],y[i]))return false;return true;
    }
    if(a.Is<double>()&&b.Is<double>()) {
        const double x=a.To<double>(),y=b.To<double>();const double delta=key=="rotation"||key=="startAngle"?std::remainder(x-y,Tau):x-y;
        return std::abs(delta)<=1e-7*std::max({1.,std::abs(x),std::abs(y)});
    }
    return SameValue(a,b);
}
VariantArray BevelSegments(const T::SRegionLoop2& loop,const std::vector<G::CurveSegment2>& boundary) {
    std::set<std::size_t> selected;
    for(const auto& curve:boundary) {
        std::vector<std::size_t> matches;auto reverse=curve;reverse.Range.Reversed=!reverse.Range.Reversed;
        const auto projected=Segment(curve),backward=Segment(reverse);
        for(std::size_t i=0;i<loop.Curves.size();++i) {
            const auto original=Segment(loop.Curves[i].Segment);
            if(NearCurveValue(original,projected)||NearCurveValue(original,backward))matches.push_back(i);
        }
        if(matches.empty())continue; // A multi-loop region may bind another loop.
        if(matches.size()!=1)throw std::invalid_argument("侧壁坡口边界无法唯一绑定当前精确曲线段");
        selected.insert(matches.front());
    }
    VariantArray result;for(const auto index:selected)result.emplace_back(static_cast<double>(index));return result;
}
ObjectMap SideView(const T::CTubeCadDocument& doc,iCAX::Project::ISceneContext& scene) {
    const auto service=scene.Services().Resolve<iCAX::CAD::ITubeCadDocumentService>();
    // The unfolding editor needs authoring operands, not the evaluated core
    // shape. Core restoration deliberately has no construction-body cache.
    const auto evaluated=service->EvaluateConstruction(doc);const auto& stock=Stock(doc);
    const auto blank=Native(evaluated.ConstructionBodies.at(doc.Construction.BaseNodeID));
    const auto reference=UnfoldTubeSideSketchCurveReference(blank);
    if(!reference.bOK)throw std::invalid_argument(reference.Diagnostic);
    auto unfolding=SerializeTubeBRepUnfolding(reference);
    VariantArray entities,bindings,readOnly;ObjectMap endCuts;
    for(const auto& node:doc.Construction.SolidNodes) {
        if(node.Metadata.contains("constructionOwner")&&node.Metadata.at("constructionOwner")!=node.ID)continue;
        if(const auto* plane=std::get_if<T::SHalfSpaceNode>(&node.Data);plane&&node.Metadata.contains("sideEnd")) {
            const auto dot=[](const auto& a,const auto& b){return a.X*b.X+a.Y*b.Y+a.Z*b.Z;};
            const G::Vector3 delta{plane->Boundary.Location.X-stock.Frame.Location.X,
                plane->Boundary.Location.Y-stock.Frame.Location.Y,plane->Boundary.Location.Z-stock.Frame.Location.Z};
            double z=dot(plane->Boundary.Normal,stock.Frame.ZDirection),x=dot(plane->Boundary.Normal,stock.Frame.XDirection),y=dot(plane->Boundary.Normal,stock.Frame.YDirection);
            if(z<0){z=-z;x=-x;y=-y;}
            const auto end=node.Metadata.at("sideEnd");
            endCuts[end]=ObjectMap{{"position",dot(delta,stock.Frame.ZDirection)-stock.First},
                {"angleDegrees",std::atan2(std::hypot(x,y),z)*180/std::numbers::pi},
                {"rotationDegrees",std::atan2(-y,-x)*180/std::numbers::pi}};
            bindings.emplace_back(ObjectMap{{"end",end},{"nodeId",node.ID}});continue;
        }
        const auto* wrapped=std::get_if<T::SWrappedVolumeNode>(&node.Data);
        if(wrapped&&!wrapped->UVRegion.Boundaries.empty()&&wrapped->Support.ExtrusionNodeID==doc.Construction.BaseNodeID
            &&wrapped->Support.LoopID==stock.Section.Boundaries.front().ID) {
            try {
                VariantArray localEntities,localBindings;
                for(const auto& loop:wrapped->UVRegion.Boundaries) {
                    const auto id=node.ID+"/"+loop.ID;auto entity=Entity(loop,id);
                    if(wrapped->UVRegion.Boundaries.size()>1) {
                        entity["fillGroup"]=node.ID;entity["fillRule"]=std::string("evenodd");
                    }
                    for(const auto& other:doc.Construction.SolidNodes)if(const auto* modifier=std::get_if<T::STopologyBevelNode>(&other.Data))
                        for(const auto& selection:modifier->Selections)if(selection.WrappedSourceNodeID==node.ID) {
                            auto bevel=ObjectMap{{"enabled",true},{"side",selection.Side},{"angleDegrees",selection.AngleDegrees},{"rootFace",selection.RootFace}};
                            const auto indices=BevelSegments(loop,selection.UVBoundary);
                            if(indices.empty())continue;
                            if(indices.size()!=loop.Curves.size())bevel["segmentIndices"]=indices;
                            entity["bevel"]=bevel;
                            break;
                        }
                    localEntities.emplace_back(entity);
                    localBindings.emplace_back(ObjectMap{{"entityId",id},{"nodeId",node.ID},{"loopId",loop.ID}});
                }
                entities.insert(entities.end(),localEntities.begin(),localEntities.end());bindings.insert(bindings.end(),localBindings.begin(),localBindings.end());
                continue;
            } catch(const std::invalid_argument&) {}
        }
        if(node.ID!=doc.Construction.BaseNodeID&&!std::holds_alternative<T::SBooleanNode>(node.Data))
            readOnly.emplace_back(ObjectMap{{"nodeId",node.ID},{"label",node.Label}});
    }
    ObjectMap sketch{{"schema",std::string("icax.tube-sketch")},{"schemaVersion",1ull},{"kind",std::string("side")},
        {"unit",std::string("mm")},{"coordinateSpace",std::string("arc-length-axial")},{"perimeter",reference.Perimeter},
        {"length",reference.Length},{"faceHeight",reference.Perimeter},{"trajectoryWidth",.5},
        {"entities",entities},{"arrays",VariantArray{}},{"splitParts",false}};
    if(!endCuts.empty())sketch["endCuts"]=endCuts;
    const auto actual=ShapeBounds(Native(service->Evaluate(doc).Shape));
    const double actualFirst=ToDouble(actual.at("min").To<VariantArray>()[0],"piece min X");
    const double sourceOffset=stock.Frame.Location.X+stock.First*stock.Frame.ZDirection.X;
    return {{"mode",std::string("side")},{"reference",ObjectMap{{"available",true},{"length",reference.Length},
        {"sourceDomain",ObjectMap{{"axialFirst",0.},{"axialLast",reference.Length},{"coordinateSpace",std::string("arc-length-axial")}}},
        {"actualPiece",ObjectMap{{"sourceAxialFirst",actualFirst-sourceOffset},{"sourceAxialLast",actualFirst+GetDouble(actual,"width",0)-sourceOffset},
            {"sourceToPartTranslation",VariantArray{stock.Frame.Location.X,stock.Frame.Location.Y,stock.Frame.Location.Z}},{"bounds",actual}}},
        {"profile",Profile(stock.Section)},{"unfolding",unfolding}}},{"sketch",sketch},{"bindings",bindings},{"readOnlyNodes",readOnly}};
}
void ReplaceReferences(T::CTubeCadDocument&,const std::string&,const std::string&);
void NormalizeBooleans(T::CTubeCadDocument&);
T::CTubeCadDocument EditSide(const T::CTubeCadDocument& original,const ObjectMap& edit,iCAX::Project::ISceneContext& scene,double wallDepth) {
    auto doc=original;const auto view=SideView(original,scene);const auto current=view.at("sketch").To<ObjectMap>();
    const auto sketch=GetRequiredObject(edit,"sketch");
    if(!SameValue(Array(edit,"bindings"),view.at("bindings")))
        throw std::invalid_argument("二维 CAD 绑定已改变，请重新打开绘图页面");
    std::map<std::string,ObjectMap> incoming;
    for(const auto& value:Array(sketch,"entities")) {auto e=value.To<ObjectMap>();
        const auto id=GetRequiredText(e,"id",240);if(!incoming.emplace(id,e).second)throw std::invalid_argument("二维图形标识重复");}
    std::map<std::string,std::vector<std::string>> nodeEntities;
    std::map<std::string,ObjectMap> prior;
    for(const auto& value:current.at("entities").To<VariantArray>()) {auto e=value.To<ObjectMap>();prior.emplace(GetString(e,"id"),e);}
    for(const auto& value:view.at("bindings").To<VariantArray>()) {const auto b=value.To<ObjectMap>();
        if(b.contains("entityId"))nodeEntities[GetString(b,"nodeId")].push_back(GetString(b,"entityId"));}
    std::map<std::string,std::string> editedIds;
    for(const auto& [node,ids]:nodeEntities) {
        bool unchanged=true;for(const auto& id:ids)if(!incoming.contains(id)||!SameValue(incoming.at(id),prior.at(id)))unchanged=false;
        if(unchanged) {for(const auto& id:ids)incoming.erase(id);} else {
            for(auto& other:doc.Construction.SolidNodes)if(auto* modifier=std::get_if<T::STopologyBevelNode>(&other.Data))
                std::erase_if(modifier->Selections,[&](const auto& selection){return selection.WrappedSourceNodeID==node;});
            std::vector<std::pair<std::string,std::string>> emptyModifiers;
            for(const auto& other:doc.Construction.SolidNodes)if(const auto* modifier=std::get_if<T::STopologyBevelNode>(&other.Data);modifier&&modifier->Selections.empty())emptyModifiers.emplace_back(other.ID,modifier->Child);
            for(const auto& [id,child]:emptyModifiers) {ReplaceReferences(doc,id,child);std::erase_if(doc.Construction.SolidNodes,[&](const auto& n){return n.ID==id;});}
            for(const auto& id:ids)if(incoming.contains(id))editedIds[id]=node;
            Remove(doc,node);NormalizeBooleans(doc);
        }
    }
    const auto service=scene.Services().Resolve<iCAX::CAD::ITubeCadDocumentService>();
    const auto stock=Stock(doc);const auto support=T::SBoundarySurfaceRef{doc.Construction.BaseNodeID,stock.Section.Boundaries.front().ID,{}};
    // The finite inward offset is geometric cutter depth, independent of any
    // manufacturing process. A support-specific depth is verified on creation.
    const double depth=0.0; // FirstHostWall observes the actual continuous wall.
    std::map<std::string,VariantArray> groups;
    for(const auto& [entityId,entity]:incoming) {
        const auto key=GetString(entity,"fillRule")=="evenodd"&&!GetString(entity,"fillGroup").empty()
            ?"group/"+GetString(entity,"fillGroup"):"entity/"+entityId;
        groups[key].emplace_back(entity);
    }
    for(const auto& [group,entities]:groups) {
        const auto entityId=GetString(entities.front().To<ObjectMap>(),"id");
        auto single=sketch;single["entities"]=entities;single.erase("endCuts");
        const auto regions=BuildTubeSideSketchPlanarRegions(single,GetDouble(current,"length",0),GetDouble(current,"perimeter",0),1e-7,false);
        if(!regions.bOK)throw std::invalid_argument(regions.Diagnostic);
        T::CPlanarRegion2 region;
        for(const auto& face:regions.Faces) {
            auto part=service->ReadPlanarRegion(BRep(face),G::Placement3{});
            for(auto& loop:part.Boundaries) {loop.ID="boundary-"+std::to_string(region.Boundaries.size()+1);region.Boundaries.push_back(std::move(loop));}
        }
        if(region.Boundaries.empty())continue;
        const auto id=editedIds.contains(entityId)?editedIds.at(entityId):"side-region/"+UuidToString(iCAX::Data::GenerateNewUUID());
        T::SSolidNode node{id,"侧壁绘制区域",T::SWrappedVolumeNode{support,region,-depth,.2,T::EWrappedDepthRule::FirstHostWall}};
        node.Relations.emplace();node.Relations->DependsOnNodeIDs={doc.Construction.BaseNodeID};
        T::SFeatureMaterialEffect effect;effect.HostNodeID=doc.Construction.BaseNodeID;effect.SourceNodeIDs={id};effect.Role=T::EFeatureMaterialRole::Penetration;node.Relations->MaterialEffect=effect;
        node.Metadata["workspace"]="side";AddOperand(doc,std::move(node));
        if(!regions.Bevels.empty()) {
            T::STopologyBevelNode modifier;modifier.Child=doc.Construction.RootNodeID;
            for(const auto& request:regions.Bevels) {
                T::STopologyBevelSelection selection;selection.GroupID=id;selection.WrappedSourceNodeID=id;
                selection.Side=GetString(request.Parameters,"side","outer");selection.AngleDegrees=GetDouble(request.Parameters,"angleDegrees",45);selection.RootFace=GetDouble(request.Parameters,"rootFace",0);
                for(const auto& boundary:request.BoundaryEdges) {
                    const auto curves=service->ReadPlanarCurves(BRep(boundary),G::Placement3{});
                    selection.UVBoundary.insert(selection.UVBoundary.end(),curves.begin(),curves.end());
                }
                modifier.Selections.push_back(selection);
            }
            const auto modifierId="side-opening-modifier/"+UuidToString(iCAX::Data::GenerateNewUUID());
            doc.Construction.SolidNodes.push_back({modifierId,"侧壁开口坡口",modifier});doc.Construction.RootNodeID=modifierId;
        }
    }
    const auto oldEnds=current.contains("endCuts")?GetRequiredObject(current,"endCuts"):ObjectMap{};
    const auto newEnds=sketch.contains("endCuts")?GetRequiredObject(sketch,"endCuts"):ObjectMap{};
    if(!SameValue(oldEnds,newEnds)) {
        for(const auto& b:view.at("bindings").To<VariantArray>()) {const auto binding=b.To<ObjectMap>();
            if(binding.contains("end"))Remove(doc,GetString(binding,"nodeId"));}
        for(const auto& [end,value]:newEnds) {
            if(end!="start"&&end!="end")throw std::invalid_argument("二维端部选择无效");
            const auto cut=value.To<ObjectMap>();const double position=GetDouble(cut,"position",end=="start"?0:stock.Last-stock.First);
            const double angle=GetDouble(cut,"angleDegrees",0),rotation=GetDouble(cut,"rotationDegrees",0);
            if(position<0||position>stock.Last-stock.First||std::abs(angle)>=85||!std::isfinite(rotation))
                throw std::invalid_argument("二维端部位置或角度无效");
            if(std::abs(position-(end=="start"?0:stock.Last-stock.First))<1e-8&&std::abs(angle)<1e-8)continue;
            const double slope=std::tan(angle*std::numbers::pi/180),phi=rotation*std::numbers::pi/180;
            auto normal=stock.Frame.ZDirection;
            normal.X-=slope*(std::cos(phi)*stock.Frame.XDirection.X+std::sin(phi)*stock.Frame.YDirection.X);
            normal.Y-=slope*(std::cos(phi)*stock.Frame.XDirection.Y+std::sin(phi)*stock.Frame.YDirection.Y);
            normal.Z-=slope*(std::cos(phi)*stock.Frame.XDirection.Z+std::sin(phi)*stock.Frame.YDirection.Z);
            auto origin=stock.Frame.Location;const auto station=stock.First+position;
            origin.X+=station*stock.Frame.ZDirection.X;origin.Y+=station*stock.Frame.ZDirection.Y;origin.Z+=station*stock.Frame.ZDirection.Z;
            const auto id="side-end/"+UuidToString(iCAX::Data::GenerateNewUUID());
            T::SSolidNode node{id,end=="start"?"起端裁切平面":"末端裁切平面",
                T::SHalfSpaceNode{G::Plane3{origin,normal,stock.Frame.XDirection},end=="end"}};
            node.Metadata["sideEnd"]=end;AddOperand(doc,std::move(node),T::EBooleanOperation::Intersection);
        }
    }
    return doc;
}
// The profile package is the current section-picker geometry protocol. Its
// analytic curves differ from the side drawing's x/y segment protocol.
ObjectMap DrawingProfile(const T::CPlanarRegion2& region,G::Point2 center={}) {
    VariantArray contours;
    const auto point=[&](const G::Point2& p){return VariantArray{p.X-center.X,p.Y-center.Y};};
    for(const auto& loop:region.Boundaries) {
        VariantArray segments;
        for(const auto& edge:loop.Curves) {
            const auto& range=edge.Segment.Range;
            std::visit([&](const auto& c) {
                using C=std::decay_t<decltype(c)>;
                if constexpr(std::is_same_v<C,G::Segment2>)
                    segments.emplace_back(ObjectMap{{"kind",std::string("line")},{"start",point(range.Reversed?c.End:c.Start)},{"end",point(range.Reversed?c.Start:c.End)}});
                else if constexpr(std::is_same_v<C,G::Circle2>) {
                    const auto at=[&](double angle) {return point({c.Placement.Location.X+c.Radius*(std::cos(angle)*c.Placement.XDirection.X+std::sin(angle)*c.Placement.YDirection.X),
                        c.Placement.Location.Y+c.Radius*(std::cos(angle)*c.Placement.XDirection.Y+std::sin(angle)*c.Placement.YDirection.Y)});};
                    const double first=range.Reversed?range.Last:range.First,last=range.Reversed?range.First:range.Last;
                    const int count=std::max(1,static_cast<int>(std::ceil(std::abs(last-first)/std::numbers::pi)));
                    for(int i=0;i<count;++i) {const double a=first+(last-first)*i/count,b=first+(last-first)*(i+1)/count;
                        segments.emplace_back(ObjectMap{{"kind",std::string("arc")},{"start",at(a)},{"middle",at((a+b)/2)},{"end",at(b)}});}
                } else if constexpr(std::is_same_v<C,G::Ellipse2>) {
                    const auto& p=c.Placement;const bool mirror=p.XDirection.X*p.YDirection.Y-p.XDirection.Y*p.YDirection.X<0;
                    segments.emplace_back(ObjectMap{{"kind",std::string("ellipseArc")},{"center",point(p.Location)},
                        {"majorRadius",c.MajorRadius},{"minorRadius",c.MinorRadius},{"rotation",std::atan2(p.XDirection.Y,p.XDirection.X)},
                        {"startAngle",mirror?-range.Last:range.First},{"endAngle",mirror?-range.First:range.Last},{"reversed",range.Reversed!=mirror}});
                } else if constexpr(std::is_same_v<C,G::Bezier2>) {
                    auto side=Segment(edge.Segment);segments.emplace_back(ObjectMap{{"kind",std::string("bezier")},{"controlPoints",side.at("points")}});
                    auto& points=std::get<VariantArray>(std::get<ObjectMap>(segments.back().m_Value).at("controlPoints").m_Value);
                    for(auto& value:points) {const auto p=value.To<VariantArray>();value=VariantArray{ToDouble(p[0],"x")-center.X,ToDouble(p[1],"y")-center.Y};}
                } else if constexpr(std::is_same_v<C,G::BSpline2>||std::is_same_v<C,G::NURBS2>) {
                    VariantArray points,knots,mults;for(const auto& p:c.Poles)points.emplace_back(point(p));
                    for(auto k:c.Knots)knots.emplace_back(k);for(auto m:c.Multiplicities)mults.emplace_back(m);
                    ObjectMap item{{"kind",std::string(std::is_same_v<C,G::NURBS2>?"nurbs":"bspline")},{"controlPoints",points},
                        {"degree",c.Degree},{"knots",knots},{"multiplicities",mults},{"periodic",c.Periodic},
                        {"startParameter",range.First},{"endParameter",range.Last},{"reversed",range.Reversed}};
                    if constexpr(std::is_same_v<C,G::NURBS2>) {VariantArray weights;for(auto w:c.Weights)weights.emplace_back(w);item["weights"]=weights;}
                    segments.emplace_back(item);
                } else throw std::invalid_argument("此精确截面尚无管型选择器投影");
            },edge.Segment.Curve);
        }
        contours.emplace_back(ObjectMap{{"kind",std::string("path")},{"closed",true},{"segments",segments}});
    }
    return {{"kind",std::string("profile-package")},{"name",std::string("CAD 精确截面")},{"contours",contours},{"editableParameters",false},{"frozenGeometry",false}};
}
G::Point2 SectionCenter(const T::CPlanarRegion2& region) {
    double xmin=std::numeric_limits<double>::max(),ymin=xmin,xmax=-xmin,ymax=-xmin;
    const auto include=[&](const G::Point2& p){xmin=std::min(xmin,p.X);xmax=std::max(xmax,p.X);ymin=std::min(ymin,p.Y);ymax=std::max(ymax,p.Y);};
    for(const auto& loop:region.Boundaries)for(const auto& edge:loop.Curves)std::visit([&](const auto& c) {
        using C=std::decay_t<decltype(c)>;
        if constexpr(std::is_same_v<C,G::Segment2>){include(c.Start);include(c.End);}
        else if constexpr(std::is_same_v<C,G::Circle2>||std::is_same_v<C,G::Ellipse2>) {
            const double a=[&]{if constexpr(std::is_same_v<C,G::Circle2>)return c.Radius;else return c.MajorRadius;}();
            const double b=[&]{if constexpr(std::is_same_v<C,G::Circle2>)return c.Radius;else return c.MinorRadius;}();
            const auto& p=c.Placement;const double dx=std::hypot(a*p.XDirection.X,b*p.YDirection.X),dy=std::hypot(a*p.XDirection.Y,b*p.YDirection.Y);
            include({p.Location.X-dx,p.Location.Y-dy});include({p.Location.X+dx,p.Location.Y+dy});
        } else if constexpr(requires {c.Poles;})for(const auto& p:c.Poles)include(p);
        else throw std::invalid_argument("截面投影缺少精确支撑曲线");
    },edge.Segment.Curve);
    if(!std::isfinite(xmin)||xmax<xmin)throw std::invalid_argument("截面投影为空");return {(xmin+xmax)/2,(ymin+ymax)/2};
}
ObjectMap FeatureView(const T::SSolidNode& node,const T::SExtrudedRegionNode& cut) {
    const auto center=SectionCenter(cut.Section);const auto& frame=cut.Frame;
    if(cut.Section.Boundaries.size()==1&&!cut.Section.Boundaries.front().Curves.empty()
        &&std::abs(frame.ZDirection.X)<1e-7
        &&(std::abs(std::abs(frame.ZDirection.Z)-1)<1e-7||std::abs(std::abs(frame.ZDirection.Y)-1)<1e-7)
        &&node.Relations&&node.Relations->Extent) {
        const auto& loop=cut.Section.Boundaries.front();const auto* circle=std::get_if<G::Circle2>(&loop.Curves.front().Segment.Curve);
        double span=0;bool full=circle!=nullptr;
        for(const auto& edge:loop.Curves) {
            const auto* arc=std::get_if<G::Circle2>(&edge.Segment.Curve);
            if(!circle||!arc||std::abs(arc->Radius-circle->Radius)>1e-7
                ||std::hypot(arc->Placement.Location.X-circle->Placement.Location.X,arc->Placement.Location.Y-circle->Placement.Location.Y)>1e-7){full=false;break;}
            span+=std::abs(edge.Segment.Range.Last-edge.Segment.Range.First);
        }
        if(full&&std::abs(span-Tau)<1e-7) {
            const double x=frame.Location.X+center.X*frame.XDirection.X+center.Y*frame.YDirection.X;
            const double y=frame.Location.Y+center.X*frame.XDirection.Y+center.Y*frame.YDirection.Y;
            const double z=frame.Location.Z+center.X*frame.XDirection.Z+center.Y*frame.YDirection.Z+(cut.First+cut.Last)/2*frame.ZDirection.Z;
            const auto& extent=*node.Relations->Extent;const bool blind=extent.Rule==T::EFeatureExtentRule::BlindFromHostBoundary;
            const bool vertical=std::abs(frame.ZDirection.Z)>1-1e-7;
            return {{"id",node.ID},{"cadNodeId",node.ID},{"type",std::string("circle")},{"recordKind",std::string("tool")},
                {"toolTarget",std::string("side")},{"toolRef",ObjectMap{{"id",std::string("circle")},{"scope",std::string("system")}}},
                {"toolParameters",ObjectMap{{"diameter",2*circle->Radius}}},{"station",x},{"face",std::string(vertical?(z>=0?"top":"bottom"):(y>=0?"right":"left"))},
                {"offset",vertical?y:z},{"rotation",0.0},{"reference",std::string("start")},{"layoutDatum",std::string("base")},
                {"blindHole",blind},{"cutDepth",blind&&extent.Depth?extent.Depth->Value:1.0},{"enabled",true},{"arrayCount",1ull},{"arrayPitch",0.0}};
        }
    }
    const double a=std::acos(std::clamp(frame.ZDirection.X,-1.0,1.0)),b=std::atan2(frame.ZDirection.Y,frame.ZDirection.Z);
    const G::Vector3 u{std::sin(a),-std::cos(a)*std::sin(b),-std::cos(a)*std::cos(b)},v{0,std::cos(b),-std::sin(b)};
    const auto dot=[](const auto& l,const auto& r){return l.X*r.X+l.Y*r.Y+l.Z*r.Z;};
    const double roll=std::atan2(dot(frame.XDirection,v),dot(frame.XDirection,u));
    const auto axial=(cut.First+cut.Last)/2;
    const G::Point3 pivot{frame.Location.X+center.X*frame.XDirection.X+center.Y*frame.YDirection.X+axial*frame.ZDirection.X,
        frame.Location.Y+center.X*frame.XDirection.Y+center.Y*frame.YDirection.Y+axial*frame.ZDirection.Y,
        frame.Location.Z+center.X*frame.XDirection.Z+center.Y*frame.YDirection.Z+axial*frame.ZDirection.Z};
    return {{"id",node.ID},{"cadNodeId",node.ID},{"type",std::string("branch-profile")},{"recordKind",std::string("branch")},
        {"toolTarget",std::string("part")},{"toolRef",ObjectMap{{"id",std::string("branch-profile")},{"scope",std::string("system")}}},
        {"toolParameters",ObjectMap{}},{"section",ObjectMap{{"source",std::string("cad")},{"name",node.Label},{"profile",DrawingProfile(cut.Section,center)}}},
        {"face",std::string("top")},{"station",pivot.X},{"reference",std::string("start")},{"layoutDatum",std::string("base")},
        {"angle",a*180/std::numbers::pi},{"azimuth",b*180/std::numbers::pi},{"roll",roll*180/std::numbers::pi},
        {"offsetY",pivot.Y},{"offsetZ",pivot.Z},{"length",cut.Last-cut.First},{"direction",std::string("symmetric")},
        {"enabled",true},{"arrayCount",1ull},{"arrayPitch",0.0},{"clearance",0.0}};
}
std::string SelectionName(const std::string& signature,const std::string& kind,std::uint64_t id) {
    return "cad-topology/"+signature+"/"+kind+"/"+std::to_string(id);
}
iCAX::OpenCascade::STubeCSGEvaluationResult NativeEvaluation(const T::CTubeCadDocument&);
std::optional<ObjectMap> EndView(const T::SSolidNode& node,const TopoDS_Shape& cutter,double length) {
    const auto source=node.Metadata.find("creationConstruction");
    if(source==node.Metadata.end()||(source->second!="end:start"&&source->second!="end:end"))return std::nullopt;
    const bool start=source->second=="end:start";std::optional<ObjectMap> result;
    if(const auto* halfSpace=std::get_if<T::SHalfSpaceNode>(&node.Data)) {
        const auto& n=halfSpace->Boundary.Normal;const auto& p=halfSpace->Boundary.Location;
        if(std::abs(n.X)<1e-7)return std::nullopt;
        const double datum=p.X+(n.Y*p.Y+n.Z*p.Z)/n.X,sy=-n.Y/n.X,sz=-n.Z/n.X;
        const double angle=std::atan(std::hypot(sy,sz))*180/std::numbers::pi;
        return ObjectMap{{"id",node.ID},{"cadNodeId",node.ID},{"type",std::string("end-miter")},
            {"toolRef",ObjectMap{{"id",std::string("end-miter")},{"scope",std::string("system")}}},{"toolParameters",ObjectMap{{"angle",angle}}},
            {"datum",std::string("center")},{"trim",start?datum:length-datum},{"angle",angle},{"rotation",std::atan2(sz,sy)*180/std::numbers::pi}};
    }
    for(TopExp_Explorer faces(cutter,TopAbs_FACE);faces.More();faces.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(faces.Current()));if(surface.GetType()!=GeomAbs_Plane)continue;
        const auto plane=surface.Plane();auto normal=plane.Axis().Direction();if(std::abs(normal.X())<1e-7)continue;
        const auto p=plane.Location();const double datum=p.X()+(normal.Y()*p.Y()+normal.Z()*p.Z())/normal.X();
        if(datum<-1e-6||datum>length+1e-6)continue;
        const double sy=-normal.Y()/normal.X(),sz=-normal.Z()/normal.X(),angle=std::atan(std::hypot(sy,sz))*180/std::numbers::pi;
        if(angle>=85)continue;
        ObjectMap value{{"id",node.ID},{"cadNodeId",node.ID},{"type",std::string("end-miter")},
            {"toolRef",ObjectMap{{"id",std::string("end-miter")},{"scope",std::string("system")}}},{"toolParameters",ObjectMap{{"angle",angle}}},
            {"datum",std::string("center")},{"trim",start?datum:length-datum},{"angle",angle},{"rotation",std::atan2(sz,sy)*180/std::numbers::pi}};
        if(result&&std::abs(GetDouble(*result,"trim",0)-GetDouble(value,"trim",0))>1e-6)return std::nullopt;
        result=value;
    }
    return result;
}
ObjectMap DrawingView(const T::CTubeCadDocument& doc,const STubePartCadContext& context) {
    VariantArray features,readOnly,bevels;ObjectMap ends{{"start",ObjectMap{{"type",std::string("keep")}}},{"end",ObjectMap{{"type",std::string("keep")}}}};
    const auto native=NativeEvaluation(doc);
    for(const auto& node:doc.Construction.SolidNodes) {
        if(node.ID==doc.Construction.BaseNodeID)continue;
        if(node.Metadata.contains("constructionOwner")&&node.Metadata.at("constructionOwner")!=node.ID)continue;
        if(const auto* bevel=std::get_if<T::STopologyBevelNode>(&node.Data)) {
            if(bevel->Selections.empty())continue;
            std::map<std::string,std::vector<const T::STopologyBevelSelection*>> groups;
            for(const auto& edge:bevel->Selections)if(edge.WrappedSourceNodeID.empty())groups[edge.GroupID].push_back(&edge);
            for(const auto& [group,selections]:groups) {
                VariantArray edgeIds,edgeNames;const auto& first=*selections.front();
                for(const auto* edge:selections){edgeIds.emplace_back(edge->EdgeID);edgeNames.emplace_back(edge->EdgeName);}
                bevels.emplace_back(ObjectMap{{"id",group},{"cadModifierId",node.ID},{"edgeIds",edgeIds},{"edgeNames",edgeNames},
                    {"referenceFaceId",first.ReferenceFaceID},{"referenceFaceName",first.FaceName},{"topologyRevision",bevel->SelectionShapeSignature},
                    {"bevel",ObjectMap{{"enabled",true},{"side",first.Side},{"angleDegrees",first.AngleDegrees},{"rootFace",first.RootFace}}}});
            }
            continue;
        }
        if(const auto shape=native.NodeShapes.find(node.ID);shape!=native.NodeShapes.end())if(const auto end=EndView(node,shape->second,Stock(doc).Last-Stock(doc).First)) {
            const auto key=node.Metadata.at("creationConstruction")=="end:start"?"start":"end";ends[key]=*end;continue;
        }
        if(const auto* cut=std::get_if<T::SExtrudedRegionNode>(&node.Data);cut&&node.Relations&&node.Relations->MaterialEffect
            &&node.Relations->MaterialEffect->HostNodeID==doc.Construction.BaseNodeID) {
            try {features.emplace_back(FeatureView(node,*cut));continue;}catch(const std::invalid_argument&){}
        }
        if(!std::holds_alternative<T::SBooleanNode>(node.Data))readOnly.emplace_back(ObjectMap{{"nodeId",node.ID},{"label",node.Label}});
    }
    ObjectMap drawing{{"schemaVersion",1ull},{"baseType",std::string("parametric")},{"name",context.Part->GetName()},
        {"quantity",context.Part->GetQuantityOverride()?context.Part->GetQuantityOverride():context.Part->GetQuantity()},
        {"material",GetString(context.Part->GetItemProperties(),"manufacturing.material")},{"bevels",bevels}};
    try {const auto& stock=Stock(doc);drawing["length"]=stock.Last-stock.First;
        drawing["section"]=ObjectMap{{"source",std::string("cad")},{"name",std::string("CAD 原材")},{"profile",DrawingProfile(stock.Section)}};
    }catch(const std::invalid_argument&) {drawing["readOnly"]=true;drawing["length"]=context.Part->GetLength();}
    return {{"mode",std::string("drawing")},{"drawing",drawing},{"features",features},{"ends",ends},
        {"readOnlyNodes",readOnly},{"bindings",VariantArray{}},{"capabilities",ObjectMap{{"bevels",true}}}};
}
iCAX::OpenCascade::STubeCSGEvaluationResult NativeEvaluation(const T::CTubeCadDocument& doc) {
    // Explicit construction preview only. Published geometry is restored by
    // ITubeCadDocumentService from the boundary core after an edit is compiled.
    const auto preview=iCAX::CAD::EvaluateTubeCadConstruction(doc);
    iCAX::OpenCascade::STubeCSGEvaluationResult result;
    result.Shape=Native(preview.Shape);result.bOK=!result.Shape.IsNull();
    result.Diagnostics=preview.Diagnostics;
    for(const auto& [id,body]:preview.ConstructionBodies)result.NodeShapes.emplace(id,Native(body));
    if(!result.bOK)throw std::invalid_argument("CAD 构造预览求值失败");
    return result;
}
gp_Trsf Frame(const G::Placement3& p) {
    gp_Trsf tr;tr.SetValues(p.XDirection.X,p.YDirection.X,p.ZDirection.X,p.Location.X,
        p.XDirection.Y,p.YDirection.Y,p.ZDirection.Y,p.Location.Y,p.XDirection.Z,p.YDirection.Z,p.ZDirection.Z,p.Location.Z);return tr;
}
G::Placement3 Placement(const gp_Trsf& p) {
    G::Placement3 out;out.Location={p.Value(1,4),p.Value(2,4),p.Value(3,4)};
    out.XDirection={p.Value(1,1),p.Value(2,1),p.Value(3,1)};out.YDirection={p.Value(1,2),p.Value(2,2),p.Value(3,2)};out.ZDirection={p.Value(1,3),p.Value(2,3),p.Value(3,3)};return out;
}
gp_Trsf FeaturePose(const ObjectMap& f,double length) {
    const double a=GetDouble(f,"angle",90)*std::numbers::pi/180,b=GetDouble(f,"azimuth",0)*std::numbers::pi/180,r=GetDouble(f,"roll",0)*std::numbers::pi/180;
    const G::Vector3 axis{std::cos(a),std::sin(a)*std::sin(b),std::sin(a)*std::cos(b)};
    const G::Vector3 u{std::sin(a),-std::cos(a)*std::sin(b),-std::cos(a)*std::cos(b)},v{0,std::cos(b),-std::sin(b)};
    const auto reference=GetString(f,"reference","start");const double s=GetDouble(f,"station",0);
    if(reference!="start"&&reference!="end"&&reference!="center")throw std::invalid_argument("构造定位基准无效");
    G::Placement3 p;p.Location={reference=="start"?s:reference=="end"?length-s:length/2+s,GetDouble(f,"offsetY",0),GetDouble(f,"offsetZ",0)};
    p.XDirection={u.X*std::cos(r)+v.X*std::sin(r),u.Y*std::cos(r)+v.Y*std::sin(r),u.Z*std::cos(r)+v.Z*std::sin(r)};
    p.YDirection={-u.X*std::sin(r)+v.X*std::cos(r),-u.Y*std::sin(r)+v.Y*std::cos(r),-u.Z*std::sin(r)+v.Z*std::cos(r)};p.ZDirection={axis.X,axis.Y,axis.Z};return Frame(p);
}
void ReplaceReferences(T::CTubeCadDocument& doc,const std::string& before,const std::string& after) {
    if(doc.Construction.RootNodeID==before)doc.Construction.RootNodeID=after;
    for(auto& node:doc.Construction.SolidNodes)std::visit([&](auto& data) {
        using D=std::decay_t<decltype(data)>;
        if constexpr(std::is_same_v<D,T::SBooleanNode>){for(auto& child:data.Children)if(child==before)child=after;}
        else if constexpr(std::is_same_v<D,T::STransformNode>||std::is_same_v<D,T::STopologyBevelNode>||std::is_same_v<D,T::SConnectedComponentNode>){if(data.Child==before)data.Child=after;}
    },node.Data);
}
void NormalizeBooleans(T::CTubeCadDocument& doc) {
    bool changed=true;while(changed) {changed=false;
        for(const auto& node:doc.Construction.SolidNodes)if(const auto* boolean=std::get_if<T::SBooleanNode>(&node.Data)) {
            if(boolean->Children.size()==1) {const auto id=node.ID,child=boolean->Children.front();ReplaceReferences(doc,id,child);
                std::erase_if(doc.Construction.SolidNodes,[&](const auto& value){return value.ID==id;});changed=true;break;}
            if(boolean->Children.empty()) {const auto id=node.ID;Remove(doc,id);changed=true;break;}
        }
    }
}
void EditBevels(T::CTubeCadDocument& doc,const VariantArray& requested,iCAX::Project::ISceneContext& scene) {
    std::vector<std::string> existing;for(const auto& node:doc.Construction.SolidNodes)if(const auto* modifier=std::get_if<T::STopologyBevelNode>(&node.Data))
        if(std::any_of(modifier->Selections.begin(),modifier->Selections.end(),[](const auto& selection){return selection.WrappedSourceNodeID.empty();}))existing.push_back(node.ID);
    if(existing.size()>1)throw std::invalid_argument("多个顺序坡口需逐级选择各自前驱拓扑");
    std::string id,child;
    if(existing.empty()){
        id=!requested.empty()?GetString(requested.front().To<ObjectMap>(),"cadModifierId"):std::string{};
        if(id.empty())id="cad-edge-modifier/"+UuidToString(iCAX::Data::GenerateNewUUID());
        if(id.size()>240||std::any_of(doc.Construction.SolidNodes.begin(),doc.Construction.SolidNodes.end(),[&](const auto& n){return n.ID==id;}))throw std::invalid_argument("坡口构造标识无效");
        child=doc.Construction.RootNodeID;
    }
    else {id=existing.front();child=std::get<T::STopologyBevelNode>(Node(doc,id).Data).Child;}
    auto predecessor=doc;predecessor.Construction.RootNodeID=child;
    const auto native=NativeEvaluation(predecessor).Shape;
    const auto signature=iCAX::CAD::CadExactTopologySignature(native);
    const auto topology=InspectPartDrawingTopology(native,*scene.Services().Resolve<iCAX::CAD::ICadBevelService>());
    T::STopologyBevelNode modifier;modifier.Child=child;modifier.SelectionShapeSignature=signature;
    std::set<std::uint64_t> selected;
    for(const auto& value:requested) {
        const auto item=value.To<ObjectMap>(),bevel=GetRequiredObject(item,"bevel");
        if(bevel.contains("enabled")&&!bevel.at("enabled").To<bool>())continue;
        const auto group=GetRequiredText(item,"id",240),side=GetString(bevel,"side","outer");
        const double angle=GetDouble(bevel,"angleDegrees",45),root=GetDouble(bevel,"rootFace",0);
        if((side!="outer"&&side!="inner"&&side!="both")||!std::isfinite(angle)||angle<=0||angle>=90||!std::isfinite(root)||root<0)
            throw std::invalid_argument("坡口侧别、角度或钝边无效");
        const auto edges=Array(item,"edgeIds"),names=Array(item,"edgeNames");
        if(edges.empty()||edges.size()!=names.size())throw std::invalid_argument("坡口需要真实边面绑定");
        const auto face=GetUInt64(item,"referenceFaceId",0);const auto faceName=GetString(item,"referenceFaceName");
        if(face&&(faceName!=SelectionName(signature,"face",face)||std::none_of(topology.Faces.begin(),topology.Faces.end(),[&](const auto& f){return f.FaceID==face;})))
            throw std::invalid_argument("坡口参考面已经改变，请在当前模型上重新选择");
        for(std::size_t i=0;i<edges.size();++i) {
            const auto edge=GetUInt64(ObjectMap{{"id",edges[i]}},"id",0);
            if(names[i].To<std::string>()!=SelectionName(signature,"edge",edge))throw std::invalid_argument("坡口所选边已经改变，请重新选择");
            const auto found=std::find_if(topology.Edges.begin(),topology.Edges.end(),[&](const auto& e){return e.EdgeID==edge;});
            if(found==topology.Edges.end()||(face&&std::find(found->AdjacentFaceIDs.begin(),found->AdjacentFaceIDs.end(),face)==found->AdjacentFaceIDs.end())||!selected.insert(edge).second)
                throw std::invalid_argument("坡口边面选择无效或重复");
            modifier.Selections.push_back({edge,face,names[i].To<std::string>(),faceName,side,angle,root,group});
        }
    }
    if(modifier.Selections.empty()) {
        if(!existing.empty()){ReplaceReferences(doc,id,child);std::erase_if(doc.Construction.SolidNodes,[&](const auto& n){return n.ID==id;});}
    } else if(existing.empty()) {doc.Construction.SolidNodes.push_back({id,"边面坡口",modifier});doc.Construction.RootNodeID=id;}
    else {auto node=std::find_if(doc.Construction.SolidNodes.begin(),doc.Construction.SolidNodes.end(),[&](const auto& n){return n.ID==id;});node->Data=modifier;}
}
std::string AttachDrawingConstruction(T::CTubeCadDocument& doc,const T::CTubeCadDocument& compiled,const std::string& target) {
    const auto* boolean=std::get_if<T::SBooleanNode>(&Node(compiled,compiled.Construction.RootNodeID).Data);
    if(!boolean||boolean->Operation!=T::EBooleanOperation::Difference||boolean->Children.size()!=2)throw std::invalid_argument("新增构造需要明确的局部减材体");
    auto part=compiled;part.Construction.RootNodeID=boolean->Children[1];
    std::erase_if(part.Construction.SolidNodes,[&](const auto& n){return n.ID==compiled.Construction.BaseNodeID||n.ID==compiled.Construction.RootNodeID;});
    const auto prefix="workspace/"+UuidToString(iCAX::Data::GenerateNewUUID())+"/";
    const auto appended=AppendTubeCadConstruction(doc,part,prefix);
    if(target.empty()||target.size()>240||std::any_of(doc.Construction.SolidNodes.begin(),doc.Construction.SolidNodes.end(),[&](const auto& n){return n.ID==target;}))throw std::invalid_argument("局部构造标识重复或无效");
    ReplaceReferences(doc,appended,target);
    for(auto& n:doc.Construction.SolidNodes)if(n.ID.starts_with(prefix)) {
        if(n.ID==appended)n.ID=target;
        n.Metadata["constructionOwner"]=target;
        if(n.Relations) {
            for(auto& dependency:n.Relations->DependsOnNodeIDs) {
                if(dependency==compiled.Construction.BaseNodeID)dependency=doc.Construction.BaseNodeID;
                else if(dependency==appended)dependency=target;
            }
            if(n.Relations->MaterialEffect) {
                if(n.Relations->MaterialEffect->HostNodeID==compiled.Construction.BaseNodeID)n.Relations->MaterialEffect->HostNodeID=doc.Construction.BaseNodeID;
                for(auto& source:n.Relations->MaterialEffect->SourceNodeIDs)if(source==appended)source=target;
            }
            if(n.Relations->Extent&&n.Relations->Extent->HostNodeID==compiled.Construction.BaseNodeID)n.Relations->Extent->HostNodeID=doc.Construction.BaseNodeID;
            if(n.Relations->Placement&&n.Relations->Placement->HostNodeID==compiled.Construction.BaseNodeID)n.Relations->Placement->HostNodeID=doc.Construction.BaseNodeID;
        }
    }
    AppendMaterialOperand(doc,target,T::EBooleanOperation::Difference);
    return target;
}
T::CTubeCadDocument EditDrawing(const T::CTubeCadDocument& original,const ObjectMap& edit,const STubePartCadContext& context,
    const iCAX::Interaction::CInvocation& request,const iCAX::Application::IApplicationContext& application,
    iCAX::Product::IProductContext* product,iCAX::Project::ISceneContext& scene) {
    auto doc=original;const auto view=DrawingView(original,context);const auto service=scene.Services().Resolve<iCAX::CAD::ITubeCadDocumentService>();
    if(edit.contains("bevels")) {
        std::vector<std::pair<std::string,std::string>> modifiers;
        for(const auto& n:doc.Construction.SolidNodes)if(const auto* modifier=std::get_if<T::STopologyBevelNode>(&n.Data))
            if(std::all_of(modifier->Selections.begin(),modifier->Selections.end(),[](const auto& selection){return selection.WrappedSourceNodeID.empty();}))modifiers.emplace_back(n.ID,modifier->Child);
        for(const auto& [id,child]:modifiers){ReplaceReferences(doc,id,child);std::erase_if(doc.Construction.SolidNodes,[&](const auto& n){return n.ID==id;});}
    }
    auto main=GetRequiredObject(view,"drawing");const double priorLength=Stock(original).Last-Stock(original).First;
    if(edit.contains("main")) {
        const auto incoming=GetRequiredObject(edit,"main");
        auto& stock=std::get<T::SExtrudedRegionNode>(std::find_if(doc.Construction.SolidNodes.begin(),doc.Construction.SolidNodes.end(),[&](const auto& n){return n.ID==doc.Construction.BaseNodeID;})->Data);
        const double length=GetDouble(incoming,"length",stock.Last-stock.First);
        if(!std::isfinite(length)||length<1||length>100000)throw std::invalid_argument("原材长度须在 1 到 100000 mm");
        const auto priorProfile=GetRequiredObject(GetRequiredObject(main,"section"),"profile");
        const auto newProfile=incoming.contains("section")?GetRequiredObject(GetRequiredObject(incoming,"section"),"profile"):priorProfile;
        if(!SameValue(newProfile,priorProfile)) {
            const auto blank=BuildPunchBlank(newProfile,length);const auto replacement=service->Import(BRep(blank)).Document;
            auto data=Stock(replacement);
            data.Frame=stock.Frame;
            for(std::size_t i=0;i<std::min(stock.Section.Boundaries.size(),data.Section.Boundaries.size());++i)data.Section.Boundaries[i].ID=stock.Section.Boundaries[i].ID;
            stock=data;
        } else stock.Last=stock.First+length;
        main=incoming;
    }
    const auto native=NativeEvaluation(doc);const auto blank=native.NodeShapes.at(doc.Construction.BaseNodeID);
    const auto profile=DrawingProfile(Stock(doc).Section);const double length=Stock(doc).Last-Stock(doc).First;
    auto endChanges=edit.contains("ends")?Array(edit,"ends"):VariantArray{};
    if(std::abs(length-priorLength)>1e-8) {
        for(const auto& end:{std::string("start"),std::string("end")}) {
            const auto before=GetRequiredObject(GetRequiredObject(view,"ends"),end);
            const auto id=GetString(before,"cadNodeId",GetString(before,"id"));
            if(id.empty()||std::any_of(endChanges.begin(),endChanges.end(),[&](const auto& value){return GetString(value.To<ObjectMap>(),"end")==end;}))continue;
            endChanges.emplace_back(ObjectMap{{"operation",std::string("update")},{"end",end},{"nodeId",id},{"feature",before}});
        }
    }
    const auto plainArray=[](const ObjectMap& f,const std::string& key) {
        if(!f.contains(key))return true;const auto values=Array(f,key);
        return values.empty()||(values.size()==1&&ToDouble(values.front(),key)==0);
    };
    std::map<std::string,ObjectMap> prior;
    for(const auto& value:view.at("features").To<VariantArray>()){auto f=value.To<ObjectMap>();prior.emplace(GetString(f,"cadNodeId"),f);}
    for(const auto& value:edit.contains("features")?Array(edit,"features"):VariantArray{}) {
        const auto change=value.To<ObjectMap>();const auto op=GetString(change,"operation");
        const auto nodeId=GetString(change,"nodeId");
        if(op!="add"&&!prior.contains(nodeId))throw std::invalid_argument("三维构造绑定已经改变，请重新读取");
        if(op=="remove") {Remove(doc,nodeId);NormalizeBooleans(doc);continue;}
        if(op!="add"&&op!="update")throw std::invalid_argument("三维构造操作无效");
        auto feature=GetRequiredObject(change,"feature");feature["id"]=op=="update"?nodeId:GetRequiredText(feature,"id",240);
        if(op=="update"&&GetString(prior.at(nodeId),"type")=="circle"&&GetString(feature,"type")=="circle"
            &&GetUInt64(feature,"arrayCount",1)==1&&GetUInt64(feature,"rowCount",1)==1
            &&GetString(feature,"face","top")==GetString(prior.at(nodeId),"face")
            &&(!feature.contains("enabled")||feature.at("enabled").To<bool>())
            &&(!feature.contains("opposite")||!feature.at("opposite").To<bool>())
            &&(!feature.contains("arrayGroups")||Array(feature,"arrayGroups").empty())
            &&(!feature.contains("arrayTransforms")||Array(feature,"arrayTransforms").empty())
            &&(!feature.contains("skippedInstances")||Array(feature,"skippedInstances").empty())
            &&plainArray(feature,"arrayOffsets")&&plainArray(feature,"rowOffsets")
            &&GetDouble(feature,"clearance",0)==GetDouble(prior.at(nodeId),"clearance",0)) {
            const auto& before=prior.at(nodeId);auto& n=*std::find_if(doc.Construction.SolidNodes.begin(),doc.Construction.SolidNodes.end(),[&](const auto& value){return value.ID==nodeId;});
            auto& cut=std::get<T::SExtrudedRegionNode>(n.Data);
            const double diameter=GetDouble(GetRequiredObject(feature,"toolParameters"),"diameter",0);
            if(!std::isfinite(diameter)||diameter<=0)throw std::invalid_argument("圆孔直径必须为正数");
            for(auto& curve:cut.Section.Boundaries.front().Curves)std::get<G::Circle2>(curve.Segment.Curve).Radius=diameter/2;
            const auto reference=GetString(feature,"reference","start");const double s=GetDouble(feature,"station",0);
            const double station=reference=="start"?s:reference=="end"?length-s:reference=="center"?length/2+s:std::numeric_limits<double>::quiet_NaN();
            if(!std::isfinite(station))throw std::invalid_argument("圆孔定位基准无效");
            cut.Frame.Location.X+=station-GetDouble(before,"station",0);
            const auto face=GetString(feature,"face","top");const bool vertical=face=="top"||face=="bottom";
            if(vertical)cut.Frame.Location.Y+=GetDouble(feature,"offset",0)-GetDouble(before,"offset",0);
            else cut.Frame.Location.Z+=GetDouble(feature,"offset",0)-GetDouble(before,"offset",0);
            const bool blind=feature.contains("blindHole")&&feature.at("blindHole").To<bool>();
            auto& extent=*n.Relations->Extent;
            if(blind) {
                const double depth=GetDouble(feature,"cutDepth",0);if(!std::isfinite(depth)||depth<=0)throw std::invalid_argument("盲孔深度必须为正数");
                extent.Rule=T::EFeatureExtentRule::BlindFromHostBoundary;extent.Depth=T::SScalarParameter{depth};
                const double outward=vertical?cut.Frame.ZDirection.Z*(face=="top"?1:-1):cut.Frame.ZDirection.Y*(face=="right"?1:-1);
                extent.Direction=outward<0?T::EExtentDirection::Positive:T::EExtentDirection::Negative;
            } else if(extent.Rule==T::EFeatureExtentRule::BlindFromHostBoundary) {extent.Rule=T::EFeatureExtentRule::ThroughSelectedHostBoundaries;extent.Depth.reset();}
            if(n.Relations->Placement)n.Relations->Placement->LocalFrame=Placement(Frame(Stock(doc).Frame).Inverted()*Frame(cut.Frame));
            continue;
        }
        const auto& before=op=="update"?prior.at(nodeId):feature;
        if(op=="update"&&feature.contains("section")&&before.contains("section")
            &&SameValue(GetRequiredObject(feature,"section"),GetRequiredObject(before,"section"))
            &&SameValue(GetRequiredObject(feature,"toolRef"),GetRequiredObject(before,"toolRef"))
            &&SameValue(GetRequiredObject(feature,"toolParameters"),GetRequiredObject(before,"toolParameters"))
            &&GetString(feature,"direction")==GetString(before,"direction")
            &&GetDouble(feature,"clearance",0)==GetDouble(before,"clearance",0)
            &&(!feature.contains("enabled")||feature.at("enabled").To<bool>())
            &&(!feature.contains("opposite")||!feature.at("opposite").To<bool>())
            &&GetUInt64(feature,"arrayCount",1)==1&&GetUInt64(feature,"rowCount",1)==1
            &&(!feature.contains("arrayGroups")||Array(feature,"arrayGroups").empty())
            &&(!feature.contains("arrayTransforms")||Array(feature,"arrayTransforms").empty())
            &&(!feature.contains("skippedInstances")||Array(feature,"skippedInstances").empty())
            &&plainArray(feature,"arrayOffsets")&&plainArray(feature,"rowOffsets")) {
            auto& node=*std::find_if(doc.Construction.SolidNodes.begin(),doc.Construction.SolidNodes.end(),[&](const auto& n){return n.ID==nodeId;});
            auto& cut=std::get<T::SExtrudedRegionNode>(node.Data);
            const auto delta=FeaturePose(feature,length)*FeaturePose(prior.at(nodeId),length).Inverted();cut.Frame=Placement(delta*Frame(cut.Frame));
            const double nextLength=GetDouble(feature,"length",cut.Last-cut.First);
            if(std::abs(nextLength-(cut.Last-cut.First))>1e-8){const auto middle=(cut.First+cut.Last)/2;cut.First=middle-nextLength/2;cut.Last=middle+nextLength/2;}
            if(node.Relations&&node.Relations->Placement)node.Relations->Placement->LocalFrame=Placement(Frame(Stock(doc).Frame).Inverted()*Frame(cut.Frame));
            continue;
        }
        if(op=="update"){Remove(doc,nodeId);NormalizeBooleans(doc);}
        std::vector<SPunchFeature> features=ReadPunchFeatures(ObjectMap{{"features",VariantArray{feature}}},"features");SPunchEnds ends;
        SPunchPreviewDiagnostics diagnostics;diagnostics.AllowDisconnectedResults=true;
        const auto userTools=product?ListPunchToolUserDataRecords(*GetUserDataStore(product)):VariantArray{};
        const auto result=EvaluatePunch(blank,profile,features,ends,request,application,scene,{},false,nullptr,nullptr,&diagnostics,false,0,userTools);
        auto compiled=CompileTubePartCadDocument(scene,blank,result,&diagnostics.ModelResult,&features);
        AttachDrawingConstruction(doc,compiled,GetString(feature,"id"));
    }
    for(const auto& value:endChanges) {
        const auto change=value.To<ObjectMap>();const auto end=GetString(change,"end"),op=GetString(change,"operation");
        if(end!="start"&&end!="end")throw std::invalid_argument("端部选择无效");
        const auto before=GetRequiredObject(GetRequiredObject(view,"ends"),end);
        const auto oldId=GetString(before,"cadNodeId",GetString(before,"id"));
        if(GetString(change,"nodeId")!=oldId)throw std::invalid_argument("端部构造绑定已经改变，请重新读取");
        if(!oldId.empty()){Remove(doc,oldId);NormalizeBooleans(doc);}
        if(op=="remove"){if(oldId.empty())throw std::invalid_argument("当前端部没有可删除构造");continue;}
        if(op!="update")throw std::invalid_argument("端部构造操作无效");
        const auto feature=GetRequiredObject(change,"feature");
        auto ends=ReadPunchEnds(ObjectMap{{"ends",ObjectMap{{end,feature}}}});
        std::vector<SPunchFeature> features;SPunchPreviewDiagnostics diagnostics;diagnostics.AllowDisconnectedResults=true;
        const auto userTools=product?ListPunchToolUserDataRecords(*GetUserDataStore(product)):VariantArray{};
        const auto result=EvaluatePunch(blank,profile,features,ends,request,application,scene,{},false,nullptr,nullptr,&diagnostics,false,0,userTools);
        const auto compiled=CompileTubePartCadDocument(scene,blank,result,&diagnostics.ModelResult,&features);
        const auto id=oldId.empty()?"cad-end/"+UuidToString(iCAX::Data::GenerateNewUUID()):oldId;
        AttachDrawingConstruction(doc,compiled,id);
        auto& node=*std::find_if(doc.Construction.SolidNodes.begin(),doc.Construction.SolidNodes.end(),[&](const auto& n){return n.ID==id;});node.Metadata["creationConstruction"]="end:"+end;
    }
    if(edit.contains("bevels"))EditBevels(doc,Array(edit,"bevels"),scene);
    return doc;
}
} // namespace TubeCadWorkspace
