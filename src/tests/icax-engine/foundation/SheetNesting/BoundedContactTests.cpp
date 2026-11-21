#include "SheetNestingBoundedContact.h"
#include "SheetNestingNfp.h"
#include "NestingNfpCombinations.h"
#include "NestingAxisSafety.h"
#include "SheetNestingConstructionTrace.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <algorithm>
#include <cmath>
#include <map>
using namespace iCAX::SheetNesting;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Part rectangle(const char* id,double w,double h,std::size_t quantity=1)
{
    Part p;p.ID=id;p.Quantity=quantity;p.Material="m";p.FreeRotation=false;p.AllowedAnglesDegrees={0,90};
    p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};return p;
}
void checkOriginalPattern(const Problem& p,const Detail::Pattern& pattern)
{
    Result result;const auto& stock=p.Stocks[0];result.Layouts.push_back({stock.ID,0,stock.Width,stock.Height,{},0});
    std::vector<std::size_t> used(p.Parts.size());double area=0;
    for(const auto& member:pattern.Members)
    {
        require(member.PartIndex<p.Parts.size(),"unknown original motif specification");
        const auto& original=p.Parts[member.PartIndex];
        result.Layouts[0].Placements.push_back({original.ID,used[member.PartIndex]++,member.Transform,1,false});
        area+=Detail::Area(original.Geometry);
    }
    for(std::size_t i=0;i<p.Parts.size();++i)
    {
        require(used[i]<=p.Parts[i].Quantity,"motif exceeded original quantities");
        if(used[i]<p.Parts[i].Quantity)result.Remaining.push_back({p.Parts[i].ID,p.Parts[i].Quantity-used[i]});
    }
    result.Layouts[0].MaterialArea=area;result.State=result.Remaining.empty()?Status::Complete:Status::Partial;
    require(Validate(p,result).Valid,"motif changed original material, angles, identities or legality");
    require(std::abs(pattern.MaterialArea-area)<=p.Tolerance,"carrier area replaced original material area");
}
int main() try
{
    Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"s",10,10,1,1,"m"});p.Parts.push_back(rectangle("R",5,10,2));
    Options options;options.TimeLimitSeconds=2;options.MaxPlacementAttempts=100000;
    auto solver=MakeBoundedContactFactory()();auto result=solver->Run(p,options);auto check=Validate(p,result);
    require(check.Valid&&check.Complete&&check.PlacedCount==2,"simple full order failed");
    require(result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"attempt budget exceeded");
    auto gap=p;gap.PartGap=1;gap.Stocks[0].Width=11;
    result=solver->Run(gap,options);check=Validate(gap,result);
    require(check.Valid&&check.Complete&&check.PlacedCount==2,"positive gap failed");
    auto partial=p;partial.Parts.clear();partial.Parts.push_back(rectangle("A",6,6,3));
    auto capped=options;capped.TimeLimitSeconds=.4;capped.MaxPlacementAttempts=5000;
    result=solver->Run(partial,capped);check=Validate(partial,result);
    require(check.Valid&&!check.Complete&&check.PlacedCount<=2,"partial result invalid");
    require(result.Stats.PlacementAttempts<=capped.MaxPlacementAttempts,"partial attempt budget exceeded");
    auto shared=std::make_shared<NfpCache>();auto factory=MakeBoundedContactFactory(shared);
    Result first,second;auto execute=[&](Result& dst){auto worker=factory();dst=worker->Run(partial,options);};
    std::thread t1(execute,std::ref(first)),t2(execute,std::ref(second));t1.join();t2.join();
    require(Validate(partial,first).Valid&&Validate(partial,second).Valid,"shared cache concurrent jobs invalid");
    require(first.Stats.PlacementAttempts<=options.MaxPlacementAttempts&&second.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"shared cache budget exceeded");
    const auto st=shared->Statistics();
    if(!st.RegionMisses){std::cerr<<"placed="<<Validate(partial,first).PlacedCount<<" attempts="<<first.Stats.PlacementAttempts<<'\n';for(const auto& msg:first.Diagnostics)std::cerr<<msg<<'\n';}
    require(st.RegionMisses>0,"shared cache was not exercised");
    auto stopped=options;stopped.Cancel=[] {return true;};result=solver->Run(p,stopped);check=Validate(p,result);
    require(check.Valid&&!check.Complete&&result.State==Status::Cancelled,"cancelled result invalid");
    auto zero=options;zero.TimeLimitSeconds=0;result=solver->Run(p,zero);check=Validate(p,result);
    require(check.Valid&&!check.Complete&&check.PlacedCount==0,"zero time result invalid");
    // Directly check that generated multi-member patterns use real geometry,
    // respect inventory and angles, and can be interrupted.
    Problem combined;combined.Tolerance=1e-7;combined.Stocks.push_back({"s",12,12,1,1,"m"});
    combined.Parts.push_back(rectangle("tile",2,2,4));combined.Parts[0].AllowedAnglesDegrees={0};
    NfpCache combCache;std::vector<NfpPreparedShape> prepared{combCache.Prepare(combined.Parts[0].Geometry)};
    Detail::NfpCombinationOptions cfg;cfg.MaxResults=90;cfg.MaxStates=96;cfg.MaxAttempts=12000;
    std::size_t attempts=0;auto patterns=Detail::BuildNfpCombinationPatterns(combined,combCache,prepared,cfg,attempts);
    bool triple=false,quad=false;
    for(const auto& pattern:patterns)
    {
        triple|=pattern.Members.size()==3;quad|=pattern.Members.size()==4;
        require(pattern.Members.size()<=4&&pattern.MaterialArea/(pattern.Width*pattern.Height)>=.85-1e-8,"invalid pattern density or inventory");
        for(std::size_t i=0;i<pattern.Members.size();++i)
        {
            require(Detail::AngleAllowed(combined.Parts[0],pattern.Members[i].Transform.AngleDegrees,combined.Tolerance),"forbidden angle");
            for(std::size_t j=0;j<i;++j)require(!Detail::ShapesConflict(combined.Parts[0].Geometry,pattern.Members[i].Transform,combined.Parts[0].Geometry,pattern.Members[j].Transform,0,combined.Tolerance),"overlapping pattern members");
        }
    }
    if(!triple||!quad){std::cerr<<"patterns="<<patterns.size()<<" attempts="<<attempts<<" triple="<<triple<<" quad="<<quad<<"\n";for(const auto& v:patterns)std::cerr<<v.Members.size()<<":"<<v.Width<<"x"<<v.Height<<" ";std::cerr<<"\n";}
    require(triple&&quad,"three and four member patterns missing");
    require(attempts<=cfg.MaxAttempts,"combination budget exceeded");
    Problem sloped;sloped.Tolerance=1e-7;sloped.Stocks.push_back({"s",10382,4900,1,1,"m"});
    Part angled;angled.ID="sloped";angled.Quantity=4;angled.Material="m";angled.FreeRotation=false;
    angled.AllowedAnglesDegrees={0,180};
    angled.Geometry.Outer={{-1091.5,-657},{669.5,-830},{1091.5,-180},{1091.5,180},{669.5,830},{-1091.5,657}};
    sloped.Parts.push_back(angled);NfpCache angledCache;
    std::vector<NfpPreparedShape> angledPrepared{angledCache.Prepare(angled.Geometry)};
    attempts=0;patterns=Detail::BuildNfpCombinationPatterns(sloped,angledCache,angledPrepared,cfg,attempts);
    bool complementary=false,slopedTriple=false,slopedFour=false;
    for(const auto& pattern:patterns)
    {
        const auto fill=pattern.MaterialArea/(pattern.Width*pattern.Height);
        require(pattern.Width<=sloped.Stocks[0].Width+sloped.Tolerance&&pattern.Height<=sloped.Stocks[0].Height+sloped.Tolerance,"sloped assembly exceeds stock");
        for(std::size_t i=0;i<pattern.Members.size();++i)
        {
            require(Detail::AngleAllowed(angled,pattern.Members[i].Transform.AngleDegrees,sloped.Tolerance),"sloped forbidden angle");
            for(std::size_t j=0;j<i;++j)require(!Detail::ShapesConflict(angled.Geometry,pattern.Members[i].Transform,angled.Geometry,pattern.Members[j].Transform,0,sloped.Tolerance),"sloped assembly overlaps");
        }
        if(pattern.Members.size()==2&&fill>.87&&
            pattern.Members[0].Transform.AngleDegrees!=pattern.Members[1].Transform.AngleDegrees&&
            !Detail::ShapesConflict(angled.Geometry,pattern.Members[0].Transform,angled.Geometry,pattern.Members[1].Transform,0,sloped.Tolerance))complementary=true;
        if(pattern.Members.size()==3&&fill>.887)slopedTriple=true;
        if(pattern.Members.size()==4&&fill>.87)slopedFour=true;
    }
    require(complementary,"sloped edge complementary pair missing");
    require(slopedTriple,"dense sloped triple missing");
    require(slopedFour,"dense sloped four member assembly missing");
    // Opposed isosceles triangles form a real solid parallelogram whose AABB
    // is only two-thirds full. A deliberately lower utilization threshold
    // exercises its independent material proof and true cell-edge repeats;
    // recognizing the carrier must not count its empty AABB corners as steel.
    Problem triangles;triangles.Tolerance=1e-7;triangles.Stocks={{"s",20,20,1,1,"m"}};
    Part triangle;triangle.ID="triangle";triangle.Material="m";triangle.Quantity=4;triangle.FreeRotation=false;
    triangle.AllowedAnglesDegrees={0,180};triangle.Geometry.Outer={{0,0},{4,0},{2,3}};triangles.Parts={triangle};
    NfpCache triangleCache;std::vector<NfpPreparedShape> trianglePrepared{triangleCache.Prepare(triangle.Geometry)};
    Detail::NfpCombinationOptions triangleOptions=cfg;triangleOptions.MaxMembers=4;
    triangleOptions.MinimumFinalFill=.6;
    attempts=0;auto trianglePatterns=Detail::BuildNfpCombinationPatterns(triangles,triangleCache,trianglePrepared,triangleOptions,attempts);
    bool solidPair=false,solidRepeated=false;
    for(const auto& pattern:trianglePatterns)
    {
        checkOriginalPattern(triangles,pattern);const auto carrier=Detail::AssessNfpCombinationCarrier(triangles,pattern);
        if(carrier.Filled&&carrier.Kind==ShapeKind::Parallelogram&&pattern.MaterialArea/(pattern.Width*pattern.Height)<.85)
        {
            require(std::abs(carrier.Fill-1)<1e-12&&std::abs(carrier.Area-pattern.MaterialArea)<1e-7,
                "full skew material carrier has incorrect area or density");
            solidPair|=pattern.Members.size()==2;solidRepeated|=pattern.Members.size()==4;
        }
    }
    require(solidPair&&solidRepeated,"dense triangle parallelogram pair or edge-vector repeat missing");
    require(attempts<=triangleOptions.MaxAttempts,"carrier recognition exceeded combination budget");
    auto highThreshold=triangleOptions;highThreshold.MinimumFinalFill=.85;attempts=0;
    const auto highPatterns=Detail::BuildNfpCombinationPatterns(triangles,triangleCache,trianglePrepared,highThreshold,attempts);
    for(const auto& pattern:highPatterns)
    {
        checkOriginalPattern(triangles,pattern);
        require(pattern.Members.size()==2||pattern.MaterialArea/(pattern.Width*pattern.Height)>=.85,
            "filled carrier recognition inflated a larger assembly's AABB utilization");
    }
    auto legacy=triangleOptions;legacy.RecognizeFilledBasicCarriers=false;legacy.MaxMembers=2;
    legacy.MinimumIntermediateFill=legacy.MinimumFinalFill=.85;attempts=0;
    const auto legacyPatterns=Detail::BuildNfpCombinationPatterns(triangles,triangleCache,trianglePrepared,legacy,attempts);
    require(std::none_of(legacyPatterns.begin(),legacyPatterns.end(),[](const auto& pattern)
        {return pattern.Kind==ShapeKind::Parallelogram&&pattern.MaterialArea/(pattern.Width*pattern.Height)<.85;}),
        "explicit high AABB threshold unexpectedly retained the low-utilization skew carrier");
    // Unlike specifications, source offsets, unequal quantities and restricted
    // directions must survive a mixed triangle/pentagon carrier unchanged.
    Problem mixed=triangles;mixed.Stocks[0].Width=30;
    Part small=triangle;small.ID="small-offset";small.Quantity=3;small.AllowedAnglesDegrees={0};
    small.Geometry.Outer={{13,-7},{15,-7},{14,-5.5}};
    Part complement=small;complement.ID="pentagon-offset";complement.Quantity=2;
    complement.Geometry.Outer={{-3,11},{-1,11},{1,14},{-3,14},{-4,12.5}};
    mixed.Parts={small,complement,triangle};NfpCache mixedCache;std::vector<NfpPreparedShape> mixedPrepared;
    for(const auto& original:mixed.Parts)mixedPrepared.push_back(mixedCache.Prepare(original.Geometry));
    std::size_t certifiedMixedBasic=0;double certifiedMixedUtilization=0;Detail::Pattern certifiedMixed;
    triangleOptions.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source!="preparation.certified-pattern")return;
        bool a=false,b=false;for(const auto& member:event.Members){a|=member.PartIndex==0;b|=member.PartIndex==1;}
        if(!a||!b)return;
        const auto kind=std::find_if(event.Measures.begin(),event.Measures.end(),[](const auto& item){return item.first=="patternKind";});
        if(kind==event.Measures.end()||(kind->second!=double(ShapeKind::Rectangle)&&kind->second!=double(ShapeKind::Parallelogram)))return;
        ++certifiedMixedBasic;
        if(event.Fill<=certifiedMixedUtilization)return;
        certifiedMixedUtilization=event.Fill;certifiedMixed={};
        certifiedMixed.Width=event.Width;certifiedMixed.Height=event.Height;certifiedMixed.MaterialArea=event.MaterialArea;
        for(const auto& member:event.Members)certifiedMixed.Members.push_back({member.PartIndex,member.Transform});
    };
    attempts=0;const auto mixedPatterns=Detail::BuildNfpCombinationPatterns(mixed,mixedCache,mixedPrepared,triangleOptions,attempts);
    triangleOptions.ConstructionTrace={};
    const auto demand=[](const Detail::Pattern& pattern)
    {std::map<std::size_t,std::size_t> result;for(const auto& member:pattern.Members)++result[member.PartIndex];return result;};
    // This exact two-original carrier remains a geometry proof even when a
    // tighter irregular arrangement wins the bounded result slots.
    Detail::Pattern originalCarrier;originalCarrier.Members={{0,{-18,18,0}},{1,{0,0,0}}};
    Detail::NormalizePattern(originalCarrier,mixed);checkOriginalPattern(mixed,originalCarrier);
    const auto originalProof=Detail::AssessNfpCombinationCarrier(mixed,originalCarrier);
    require(originalProof.Filled&&std::abs(originalProof.Area-originalCarrier.MaterialArea)<1e-8,
        "offset unlike originals lost their independent real-material carrier proof");
    require(certifiedMixedBasic>0&&!certifiedMixed.Members.empty(),
        "bounded mixed construction produced no certified basic carrier to compare with its retained alternatives");
    checkOriginalPattern(mixed,certifiedMixed);
    require(Detail::AssessNfpCombinationCarrier(mixed,certifiedMixed).Filled,
        "a certified mixed basic carrier was not backed by its original material union");
    const auto originalDemand=demand(originalCarrier),certifiedDemand=demand(certifiedMixed);
    double retainedOriginalUtilization=0,retainedCertifiedUtilization=0;
    for(const auto& pattern:mixedPatterns)
    {
        checkOriginalPattern(mixed,pattern);const auto key=demand(pattern);
        const double utilization=pattern.MaterialArea/(pattern.Width*pattern.Height);
        if(key==originalDemand)retainedOriginalUtilization=std::max(retainedOriginalUtilization,utilization);
        if(key==certifiedDemand)retainedCertifiedUtilization=std::max(retainedCertifiedUtilization,utilization);
    }
    const double originalUtilization=originalCarrier.MaterialArea/(originalCarrier.Width*originalCarrier.Height);
    const bool retainedTightness=retainedOriginalUtilization+1e-12>=originalUtilization&&
        retainedCertifiedUtilization+1e-12>=certifiedMixedUtilization;
    if(!retainedTightness)
    {
        std::cerr<<"mixed carrier diagnostics attempts="<<attempts<<" returned="<<mixedPatterns.size()
            <<" certifiedMixedBasic="<<certifiedMixedBasic<<" bestCertifiedU="<<certifiedMixedUtilization<<'\n';
        for(const auto& pattern:mixedPatterns)
        {
            bool a=false,b=false;for(const auto& member:pattern.Members){a|=member.PartIndex==0;b|=member.PartIndex==1;}
            if(!a||!b)continue;
            std::cerr<<"retained mixed members="<<pattern.Members.size()<<" U="
                <<pattern.MaterialArea/(pattern.Width*pattern.Height)<<" box="<<pattern.Width<<'x'<<pattern.Height<<'\n';
        }
    }
    require(retainedTightness,"retained mixed poses were looser than the certified carrier for the same original demand");
    require(mixed.Parts[0].Geometry.Outer[0].X==13&&mixed.Parts[1].Quantity==2,
        "mixed carrier preparation mutated original geometry or demand");
    // A frame contains a real central void. Even forged planning metadata may
    // not certify that void as material in a rectangular carrier.
    Problem frame=triangles;frame.Stocks[0].Width=frame.Stocks[0].Height=4;
    frame.Parts={rectangle("horizontal",4,1,2),rectangle("vertical",1,2,2)};
    for(auto& original:frame.Parts)original.AllowedAnglesDegrees={0};
    Detail::Pattern hollow;hollow.Members={{0,{0,0,0}},{0,{0,3,0}},{1,{0,1,0}},{1,{3,1,0}}};
    hollow.Width=hollow.Height=4;hollow.MaterialArea=12;hollow.Kind=ShapeKind::Rectangle;
    checkOriginalPattern(frame,hollow);const auto hollowCarrier=Detail::AssessNfpCombinationCarrier(frame,hollow);
    require(!hollowCarrier.Filled&&hollowCarrier.Kind==ShapeKind::Irregular&&std::abs(hollowCarrier.Fill-.75)<1e-12,
        "a central void was promoted to full carrier material");
    auto disallowed=mixed;disallowed.Parts[2].AllowedAnglesDegrees={0};attempts=0;
    const auto constrained=Detail::BuildNfpCombinationPatterns(disallowed,mixedCache,mixedPrepared,triangleOptions,attempts);
    for(const auto& pattern:constrained)checkOriginalPattern(disallowed,pattern);
    // The small caps connect the large profiles; the large profiles need not
    // touch directly. Keeping only the tightest two-large-piece carrier loses
    // this path, as does spending every angle/anchor quota on the first choice.
    Problem bridge;bridge.Tolerance=1e-7;bridge.Stocks={{"s",40,60,1,1,"m"}};
    Part profile;profile.ID="long-profile";profile.Material="m";profile.Quantity=2;profile.FreeRotation=false;
    profile.AllowedAnglesDegrees={0,180};
    profile.Geometry.Outer={{-5.5,15.5},{-4.5,-13.5},{-2.5,-16.5},{.5,-15.5},{5.5,-5.5},{-.5,14.5},{-3.5,16.5}};
    Part cap=profile;cap.ID="small-cap";cap.Geometry.Outer={{-4.5,-2.5},{4.5,-2.5},{2.5,2.5},{-2.5,2.5}};
    bridge.Parts={profile,cap};NfpCache bridgeCache;
    std::vector<NfpPreparedShape> bridgePrepared{bridgeCache.Prepare(profile.Geometry),bridgeCache.Prepare(cap.Geometry)};
    attempts=0;const auto bridged=Detail::BuildNfpCombinationPatterns(bridge,bridgeCache,bridgePrepared,triangleOptions,attempts);
    bool connected=false;
    for(const auto& pattern:bridged)
    {
        checkOriginalPattern(bridge,pattern);std::size_t profiles=0,caps=0;
        for(const auto& member:pattern.Members){profiles+=member.PartIndex==0;caps+=member.PartIndex==1;}
        if(profiles==2&&caps==2&&pattern.MaterialArea/(pattern.Width*pattern.Height)>=.85-1e-10)
        {
            connected=true;require(!Detail::AssessNfpCombinationCarrier(bridge,pattern).Filled,
                "a useful partly filled bridge must not masquerade as a solid basic carrier");
            // The final normalized stored poses must satisfy nominal material
            // clearance, including contacts repaired after NFP quantization.
            for(std::size_t i=0;i<pattern.Members.size();++i)
            for(std::size_t j=0;j<i;++j)
            {
                const auto& a=pattern.Members[i];const auto& b=pattern.Members[j];
                require(AxisSafety::Pair(AxisSafety::Make(bridge.Parts[a.PartIndex].Geometry,a.Transform),
                    AxisSafety::Make(bridge.Parts[b.PartIndex].Geometry,b.Transform),
                    bridge.PartGap,bridge.AllowCommonEdges,bridge.Tolerance)==AxisSafety::Verdict::Safe,
                    "normalized small-anchor bridge violated strict nominal original-material clearance");
            }
        }
    }
    require(connected,"small-anchor bridge was lost before the fourth original could fill it");
    require(attempts<=triangleOptions.MaxAttempts,"bridge search exceeded the shared attempt budget");
    // The independent constructive job must reach its entire input, including
    // inventories larger than the legacy bounded job's empty-root limit.
    Problem bulk;bulk.Tolerance=1e-7;bulk.Stocks={{"s",16,10,1,1,"m"}};
    bulk.Parts={rectangle("small-tile",2,2,40)};bulk.Parts[0].AllowedAnglesDegrees={0};
    auto compositeBudget=options;compositeBudget.TimeLimitSeconds=5;compositeBudget.MaxPlacementAttempts=400000;
    auto compositeFactory=MakeNfpCompositeContactFactory();
    const auto bulkResult=compositeFactory()->Run(bulk,compositeBudget);const auto bulkCheck=Validate(bulk,bulkResult);
    require(bulkCheck.Valid&&bulkCheck.Complete&&bulkCheck.PlacedCount==40,
        "composite construction retained only a short prefix of a large inventory");
    require(bulkResult.Stats.PlacementAttempts<=compositeBudget.MaxPlacementAttempts,
        "composite preparation and construction did not share the attempt budget");
    for(auto placement:{NfpCompositePlacement::LowThenLeft,NfpCompositePlacement::SharedBoundary})
    {
        NfpCompositeSettings policy;policy.Placement=placement;
        const auto tiled=MakeNfpCompositeContactFactory(policy)()->Run(bulk,compositeBudget);
        require(Validate(bulk,tiled).Valid&&Validate(bulk,tiled).Complete&&Validate(bulk,tiled).PlacedCount==40,
            "independent composite placement scoring lost original tile quantities");
        require(tiled.Stats.PlacementAttempts<=compositeBudget.MaxPlacementAttempts,
            "independent composite placement scoring exceeded the shared attempt limit");
    }
    // With combinations disabled, two complementary originals must still be
    // compared across all allowed angles. Committing the first available
    // orientation spreads these two triangles across twice the necessary width.
    Problem complementarySingles;complementarySingles.Tolerance=1e-7;
    complementarySingles.Stocks={{"s",8,3,1,1,"m"}};
    auto singleTriangle=rectangle("right-triangle",4,3,2);
    singleTriangle.Geometry.Outer={{0,0},{4,0},{0,3}};singleTriangle.AllowedAnglesDegrees={0,180};
    complementarySingles.Parts={singleTriangle};
    auto singlesBudget=compositeBudget;singlesBudget.EnableCombinations=false;
    for(auto placement:{NfpCompositePlacement::LowThenLeft,NfpCompositePlacement::SharedBoundary})
    {
        NfpCompositeSettings policy;policy.Order=NfpCompositeOrder::AnchorArea;policy.Placement=placement;
        const auto tight=MakeNfpCompositeContactFactory(policy)()->Run(complementarySingles,singlesBudget);
        require(Validate(complementarySingles,tight).Valid&&Validate(complementarySingles,tight).Complete,
            "complementary single-orientation comparison lost an original triangle");
        double occupiedRight=0;
        for(const auto& placed:tight.Layouts.front().Placements)
        {
            const auto bounds=Detail::Bounds(singleTriangle.Geometry,placed.Transform);
            occupiedRight=std::max(occupiedRight,bounds.X+bounds.Width);
        }
        require(occupiedRight<=4+complementarySingles.Tolerance,
            "allowed-angle comparison failed to close the complementary triangle gap");
    }
    auto compositeCache=std::make_shared<NfpCache>();auto sharedComposite=MakeNfpCompositeContactFactory({},compositeCache);
    Result bulkA,bulkB;
    std::thread ca([&]{bulkA=sharedComposite()->Run(bulk,compositeBudget);});
    std::thread cb([&]{bulkB=sharedComposite()->Run(bulk,compositeBudget);});ca.join();cb.join();
    require(Validate(bulk,bulkA).Complete&&Validate(bulk,bulkB).Complete&&bulk.Parts[0].Quantity==40,
        "concurrent composite jobs shared demand, placements or mutable solve state");
    auto narrowBudget=compositeBudget;narrowBudget.MaxPlacementAttempts=1;
    const auto narrowResult=compositeFactory()->Run(bulk,narrowBudget);
    require(Validate(bulk,narrowResult).Valid&&narrowResult.Stats.PlacementAttempts<=1,
        "composite early budget exhaustion lost original demand or exceeded work");
    auto cancelledComposite=compositeBudget;cancelledComposite.Cancel=[] {return true;};
    const auto cancelledResult=compositeFactory()->Run(bulk,cancelledComposite);
    require(cancelledResult.State==Status::Cancelled&&Validate(bulk,cancelledResult).Valid&&cancelledResult.Remaining[0].Quantity==40,
        "composite cancellation lost original quantities");
    auto oddBridge=bridge;oddBridge.Parts[0].Quantity=3;oddBridge.Stocks[0].Width=60;
    NfpCompositeSettings preserving;preserving.Order=NfpCompositeOrder::AnchorArea;preserving.PreserveComplementaryInventory=true;
    const auto reserved=MakeNfpCompositeContactFactory(preserving)()->Run(oddBridge,compositeBudget);
    require(Validate(oddBridge,reserved).Valid&&Validate(oddBridge,reserved).Complete&&Validate(oddBridge,reserved).PlacedCount==5,
        "complementary-inventory lookahead lost an odd original or changed its quantities");
    const auto defaults=DefaultStrategies().IDs();
    require(std::none_of(defaults.begin(),defaults.end(),[](const auto& id){return id.find("nfp.composite")==0;}),
        "independent composite construction was registered as a new default job");
    combined.Parts[0].Quantity=1;attempts=0;patterns=Detail::BuildNfpCombinationPatterns(combined,combCache,prepared,cfg,attempts);
    require(patterns.empty(),"insufficient inventory generated pattern");
    combined.Parts[0].Quantity=4;cfg.Stop=[] {return true;};attempts=0;
    require(Detail::BuildNfpCombinationPatterns(combined,combCache,prepared,cfg,attempts).empty()&&attempts==0,"combination cancellation ignored");
    cfg.Stop={};cfg.MaxAttempts=0;attempts=0;
    require(Detail::BuildNfpCombinationPatterns(combined,combCache,prepared,cfg,attempts).empty()&&attempts==0,"zero combination budget ignored");
    std::cout<<"bounded contact controls passed\n";
}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
