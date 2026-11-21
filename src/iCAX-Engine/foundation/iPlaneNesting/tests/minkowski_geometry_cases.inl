// Analytic checks independent of the vendor-instruction fixture and the
// recovered containment implementation. Points are deliberately off edges.
bool minkowskiTestContains(const ConvolutionIntegerPolygon &rings,double x,double y){int winding=0;for(const auto &ring:rings){auto a=ring.back();for(auto b:ring){const double cross=(double(b[0])-a[0])*(y-a[1])-(double(b[1])-a[1])*(x-a[0]);if(a[1]<=y&&b[1]>y&&cross>0)++winding;if(b[1]<=y&&a[1]>y&&cross<0)--winding;a=b;}}return winding!=0;}
void testMinkowskiGeometry(){
    const auto rect=[](int64_t x,int64_t y,int64_t w,int64_t h,bool hole=false){ConvolutionIntegerRing r{{x,y},{x+w,y},{x+w,y+h},{x,y+h}};if(hole)std::reverse(r.begin(),r.end());return r;};
    const auto area=[](const ConvolutionIntegerPolygon &rings){double sum=0;for(const auto &ring:rings){auto a=ring.back();for(auto b:ring){sum+=double(a[0])*double(b[1])-double(a[1])*double(b[0]);a=b;}}return sum*.5;};
    for(int64_t n=0;n<120;++n){const int64_t w=20+n%19,h=30+n%17,bw=3+n%11,bh=4+n%13;
        const ConvolutionIntegerPolygon a{rect(-7,9,w,h)},b{rect(2,-3,bw,bh)};const auto out=convolutionIntegerMinkowski(a,b);check(out.roots.size()==1&&out.rings.size()==1,"Rectangle sum is one outer contour");near(area(out.rings),double((w+bw)*(h+bh)),"Analytic rectangle Minkowski area");
        for(auto p:std::array<ShakerPoint,4>{{{-4.5,6.5},{double(w+bw)-5.5,double(h+bh)+5.5},{-5.5,6.5},{double(w+bw)-4.5,6.5}}})check(minkowskiTestContains(out.rings,p[0],p[1])==(p[0]>-5&&p[0]<-5+w+bw&&p[1]>6&&p[1]<6+h+bh),"Analytic translated rectangle membership");
        const ConvolutionIntegerPolygon holed{rect(0,0,100,100),rect(20,20,60,60,true)};const auto hole=convolutionIntegerMinkowski(holed,{rect(0,0,bw,bh)});check(hole.roots.size()==1&&hole.rings.size()==2,"Minkowski retains fitting inner hole");near(area(hole.rings),double((100+bw)*(100+bh)-(60-bw)*(60-bh)),"Analytic expanded exterior and contracted hole area");check(!minkowskiTestContains(hole.rings,50.25,50.75)&&minkowskiTestContains(hole.rings,10.25,10.75),"Hole remains empty and surrounding material remains occupied");
        const auto swapped=convolutionIntegerMinkowski({rect(0,0,bw,bh)},holed);for(int x=-1;x<120;x+=7)for(int y=-1;y<120;y+=11)check(minkowskiTestContains(hole.rings,x+.25,y+.75)==minkowskiTestContains(swapped.rings,x+.25,y+.75),"Minkowski commutativity by independent winding");
    }
    const auto multi=convolutionIntegerMinkowski({rect(0,0,10,10),rect(30,0,10,10)},{rect(0,0,3,4)});check(multi.roots.size()==2&&multi.rings.size()==2,"Disjoint sum keeps two exteriors");near(area(multi.rings),364,"Analytic disjoint-component area");check(!minkowskiTestContains(multi.rings,20.25,5.75),"Gap between disconnected exteriors remains empty");
    const auto filled=convolutionIntegerMinkowski({rect(0,0,100,100),rect(20,20,60,60,true)},{rect(0,0,70,70)});check(filled.rings.size()==1&&minkowskiTestContains(filled.rings,50.25,50.75),"Oversized operand eliminates hole");near(area(filled.rings),28900,"Analytic closed-hole sum area");
    check(convolutionIntegerMinkowski({},{}).rings.empty(),"Empty sum shortcut");
    // Expected rectangle corners are calculated directly in world coordinates,
    // without the recovered scale/quantizer or integer predicates.
    for(int n=0;n<80;++n){
        const double sx=(n%4+1)*.125,sy=(n%7+1)*.03125;
        const double ax=-37.125+n*.25,ay=19.75-n*.5,bx=.03125*n,by=-.0625*n;
        const double aw=23*sx,ah=17*sy,bw=7*sx,bh=5*sy;
        const ConvolutionRing a{{ax,ay},{ax+aw,ay},{ax+aw,ay+ah},{ax,ay+ah}},b{{bx,by},{bx+bw,by},{bx+bw,by+bh},{bx,by+bh}};
        const auto out=convolutionFloatingMinkowski(a,b);check(out.size()==1&&out[0].size()==4,"Floating rectangle sum retains four corners");
        std::array<bool,4> seen{};const double x0=ax+bx,y0=ay+by,x1=x0+aw+bw,y1=y0+ah+bh;
        for(auto p:out[0]){const bool right=std::abs(p[0]-x1)<1e-7,top=std::abs(p[1]-y1)<1e-7;
            check((right||std::abs(p[0]-x0)<1e-7)&&(top||std::abs(p[1]-y0)<1e-7),"Floating rectangle world-coordinate analytic corners");
            const size_t id=size_t(right)+2*size_t(top);check(!seen[id],"Floating rectangle corners are unique");seen[id]=true;
        }
    }
    {
        auto a=std::make_shared<ConvolutionGeometry>(),b=std::make_shared<ConvolutionGeometry>();a->key=1;b->key=2;
        a->points={{0,0},{10,0},{10,8},{0,8}};b->points={{0,0},{3,0},{3,2},{0,2}};
        ConvolutionKey key;key.poses[0].geometry=a;key.poses[1].geometry=b;ConvolutionCacheAccounting accounting;int64_t calls=0;
        ConvolutionStraightRuntime runtime(accounting,[](){return int64_t(10000);},[&](){return ++calls*1000000;});double elapsed=0;
        auto source=runtime.boundary(key,elapsed),polygon=runtime.boundary(key,elapsed,ConvolutionBackend::IntegerPolygons);
        check(source!=polygon&&calls==4,"Convolution modes use independent caches for the same key");
        check(runtime.boundary(key,elapsed)==source&&runtime.boundary(key,elapsed,ConvolutionBackend::IntegerPolygons)==polygon&&calls==4,"Convolution modes preserve both cached identities");
        check(std::all_of(polygon->records.begin(),polygon->records.end(),[](auto &r){return !r.sourceAIdentity&&!r.sourceBIdentity;}),"Integer polygon records omit source-curve identities");
        key.expansion=.1;source=runtime.boundary(key,elapsed);polygon=runtime.boundary(key,elapsed,ConvolutionBackend::IntegerPolygons);
        check(source!=polygon&&source->records.size()==polygon->records.size()&&calls==8,"Expansion forces sourced computation while retaining requested cache");
        for(size_t n=0;n<source->records.size();++n){auto &x=source->records[n],&y=polygon->records[n];check(x.start==y.start&&x.end==y.end&&x.previous==y.previous&&x.next==y.next&&x.direction==y.direction&&x.sourceAIdentity==y.sourceAIdentity&&x.sourceBIdentity==y.sourceBIdentity,"Expanded polygon request matches sourced record semantics");}
        check(runtime.boundary(key,elapsed,ConvolutionBackend::SymmetricPolygons)==polygon&&calls==8,"Symmetric and integer modes share the final polygon cache");
        key.expansion=.2;source=runtime.boundary(key,elapsed);auto symmetric=runtime.boundary(key,elapsed,ConvolutionBackend::SymmetricPolygons);
        check(symmetric!=source&&symmetric->records.size()==source->records.size()&&calls==12,"Symmetry expansion also selects source computation before symmetry normalization");
        for(size_t n=0;n<source->records.size();++n)check(source->records[n].start==symmetric->records[n].start&&source->records[n].end==symmetric->records[n].end,"Symmetry expansion matches source endpoints");
        bool rejected=false;try{runtime.boundary(key,elapsed,static_cast<ConvolutionBackend>(3));}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Unknown convolution backend remains explicitly unsupported");
    }
    // Independent analytic rectangle sums exercise the live symmetry backend,
    // including reuse under a common rotation and exchanging both operands.
    for(int mirror=0;mirror<2;++mirror){
        auto a=std::make_shared<ConvolutionGeometry>(),b=std::make_shared<ConvolutionGeometry>();a->key=31;b->key=32;
        a->points={{0,0},{10,0},{10,8},{0,8}};b->points={{0,0},{3,0},{3,2},{0,2}};
        ConvolutionCacheAccounting accounting;ConvolutionStraightRuntime runtime(accounting,[](){return int64_t(0);},[](){return int64_t(0);});double elapsed=0;
        for(int quarter=0;quarter<4;++quarter)for(int swap=0;swap<2;++swap){
            ConvolutionKey key;key.poses[0]={a,quarter*1.5707963267948966,{uint8_t(mirror),0},0};key.poses[1]={b,quarter*1.5707963267948966,{uint8_t(mirror),0},0};if(swap)std::swap(key.poses[0],key.poses[1]);
            const auto output=runtime.boundary(key,elapsed,ConvolutionBackend::SymmetricPolygons);check(output->records.size()==4,"Symmetry analytic rectangle keeps four edges");
            std::vector<ShakerPoint> corners{{0,0},{13,0},{13,10},{0,10}};for(auto &p:corners){for(int n=0;n<quarter;++n)p={-p[1],p[0]};if(mirror)p[1]=-p[1];}
            for(const auto &r:output->records)for(const auto &point:{r.start,r.end})check(std::any_of(corners.begin(),corners.end(),[&](const auto &p){return std::abs(p[0]-point[0])<1e-7&&std::abs(p[1]-point[1])<1e-7;}),"Independent rotated/mirrored rectangle sum corners");
        }
    }
}
