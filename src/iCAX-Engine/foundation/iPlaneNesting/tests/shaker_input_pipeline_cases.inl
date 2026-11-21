size_t inputPipelineScenes=0,inputPipelineSolves=0;
size_t sdkPipelineScenes=0,sdkPipelineRotations=0,sdkPipelineCacheClears=0;
std::array<size_t,4> sdkPipelineModes{};
double inputPipelineEnvelopeBefore=0,inputPipelineEnvelopeAfter=0;
void testShakerInputPipeline(){
    // Independent convex clipping of a star-shaped polygon's triangle fan.
    const auto cross=[](ShakerPoint a,ShakerPoint b,ShakerPoint c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);};
    const auto intersection=[&](std::array<ShakerPoint,3> a,std::array<ShakerPoint,3> b){if(cross(a[0],a[1],a[2])<0)std::swap(a[1],a[2]);if(cross(b[0],b[1],b[2])<0)std::swap(b[1],b[2]);std::vector<ShakerPoint> polygon(a.begin(),a.end());
        for(size_t edge=0;edge<3&&!polygon.empty();++edge){std::vector<ShakerPoint> next;for(size_t n=0;n<polygon.size();++n){const auto p=polygon[n],q=polygon[(n+1)%polygon.size()];const auto dp=cross(b[edge],b[(edge+1)%3],p),dq=cross(b[edge],b[(edge+1)%3],q);if(dp>=0)next.push_back(p);if((dp>=0)!=(dq>=0)){const auto t=dp/(dp-dq);next.push_back({p[0]+(q[0]-p[0])*t,p[1]+(q[1]-p[1])*t});}}polygon=std::move(next);}
        double area=0;for(size_t n=0;n<polygon.size();++n)area+=polygon[n][0]*polygon[(n+1)%polygon.size()][1]-polygon[n][1]*polygon[(n+1)%polygon.size()][0];return std::abs(area)*.5;};
    const std::array<std::vector<ShakerPoint>,3> shapes{{{{0,0},{3,0},{3,2},{0,2}},{{0,0},{3,0},{0,2}},{{0,0},{3,0},{3,1},{1,1},{1,3},{0,3}}}};
    for(size_t trial=0;trial<48;++trial){ShakerInputScene scene;shakerConfigureOrdinaryRelations(scene.relations);
        for(const auto &shape:shapes)scene.shapes.push_back({{},{{-1,shape,false,0}}});
        const size_t count=6+trial%7;
        for(size_t n=0;n<count;++n){const auto index=shakerCreatePart(scene,n%3,{8+10*double(n%4),8+8*double(n/4)},(trial%3==0)?0:double(n%4)*std::numbers::pi/2,{uint8_t((n+trial)%2),0});shakerSetMobility(scene.parts[index],{int32_t(trial%5==0&&n==0),0,1},scene.parts[index].bounds,scene.parts[index].angleBounds);shakerSetPartLabel(scene,index,int32_t(n));}
        shakerCreateBoundaryGeometry(scene,200,200,10);check(shakerSetBounds(scene,0,60,0,40)==0,"Input pipeline boundaries");
        const auto initial=scene.parts;
        const auto world=[&](const std::vector<ShakerPart> &parts){std::vector<std::vector<ShakerPoint>> polygons;
            for(size_t n=0;n<count;++n){const auto &p=parts[n];auto polygon=shapes[n%3];const auto c=std::cos(p.angle),s=std::sin(p.angle);for(auto &q:polygon){const double x=q[0]*c-q[1]*s,y=q[0]*s+q[1]*c;q={(p.mirror[1]?-x:x)+p.position[0],(p.mirror[0]?-y:y)+p.position[1]};}polygons.push_back(std::move(polygon));}return polygons;};
        const auto envelope=[](const auto &polygons){double xmin=INFINITY,xmax=-INFINITY,ymin=INFINITY,ymax=-INFINITY;for(const auto &polygon:polygons)for(const auto &p:polygon){xmin=std::min(xmin,p[0]);xmax=std::max(xmax,p[0]);ymin=std::min(ymin,p[1]);ymax=std::max(ymax,p[1]);}return (xmax-xmin)*(ymax-ymin);};
        const auto before=envelope(world(initial));const int32_t first=trial%4<2?0:1,second=trial%2?3:2;
        const auto result=compactShakerFixedScene(scene,first,second,10);
        if(result.status||!result.iteration.success)std::cerr<<"Input pipeline trial "<<trial<<" status "<<result.status<<" solves "<<result.solves<<'\n';
        check(result.status==0&&result.iteration.success,"Input pipeline accepted saved solution");check(result.solves>1&&result.pairs>0,"Input pipeline actual iterative solve");
        const auto polygons=world(scene.parts);const auto after=envelope(polygons);check(after<before*.8,"Input pipeline tighter occupied envelope");
        for(size_t n=0;n<count;++n){check(scene.parts[n].angle==initial[n].angle&&scene.parts[n].mirror==initial[n].mirror,"Input pipeline fixed orientation");if(initial[n].locked[0])near(scene.parts[n].position[0],initial[n].position[0],"Input pipeline locked x");for(auto p:polygons[n])check(p[0]>=-2e-5&&p[0]<=60+2e-5&&p[1]>=-2e-5&&p[1]<=40+2e-5,"Input pipeline independent board containment");}
        for(size_t i=0;i<count;++i)for(size_t j=i+1;j<count;++j){double area=0;for(size_t a=1;a+1<polygons[i].size();++a)for(size_t b=1;b+1<polygons[j].size();++b)area+=intersection({polygons[i][0],polygons[i][a],polygons[i][a+1]},{polygons[j][0],polygons[j][b],polygons[j][b+1]});check(area<1e-6,"Input pipeline independent interior intersection");}
        ++inputPipelineScenes;inputPipelineSolves+=result.solves;inputPipelineEnvelopeBefore+=before;inputPipelineEnvelopeAfter+=after;
    }
    ShakerInputScene missing;check(compactShakerFixedScene(missing,0,2,1).status==4,"Input pipeline missing borders rejected");
    for (size_t trial = 0; trial < 64; ++trial) {
        const int32_t geometryMode = int32_t(trial/16)-1;
        ShakerInputScene scene;
        ConvolutionCurves curves;
        shakerConfigureOrdinaryRelations(scene.relations);
        std::array<ShakerPolygonInput, 3> inputs;
        inputs[0].outer = {{0,0},{12,0},{12,10},{0,10}};
        inputs[0].holes = {{{2,2},{10,2},{10,8},{2,8}}};
        inputs[1].outer = {{0,0},{3,0},{3,2},{0,2}};
        inputs[2].outer = {{0,0},{5,0},{0,4}};
        const std::array<ShakerPoint, 3> positions{{{10,10},{14,13},{30,10}}};
        for (size_t n = 0; n < inputs.size(); ++n) {
            const auto shape = shakerCreateGeometry(scene, {});
            const auto count = shakerAppendPolygonComponents(scene, shape, inputs[n], curves);
            check(count == (n == 0 ? 2 : 1), "SDK pipeline splits holes into real components");
            const auto part = shakerCreatePart(scene, shape, positions[n], 0);
            shakerSetMobility(scene.parts[part], {0,0,int32_t(trial%2 == 0)},
                              {0,60,0,40}, {-.08,.08});
        }
        shakerCreateBoundaryGeometry(scene, 200, 200, 10);
        check(shakerSetBounds(scene, 0, 60, 0, 40) == 0, "SDK pipeline boundaries");
        const auto result = trial%2
            ? shakeShakerScene(scene, 2, true, 10, geometryMode)
            : compactShakerFixedScene(scene, trial%4 == 0 ? 0 : 1, 2, 10, geometryMode);
        check(result.status == 0 && result.iteration.success, "SDK pipeline accepted geometry");
        const auto transform = [&](size_t index, const auto &points) {
            auto output = points;
            const auto &part = scene.parts[index];
            const auto cosine = std::cos(part.angle), sine = std::sin(part.angle);
            for (auto &point : output) {
                const auto x = point[0]*cosine-point[1]*sine;
                const auto y = point[0]*sine+point[1]*cosine;
                point = {x+part.position[0],y+part.position[1]};
            }
            return output;
        };
        const auto overlap = [&](const auto &a, const auto &b) {
            double area = 0;
            for (size_t i = 1; i+1 < a.size(); ++i)
                for (size_t j = 1; j+1 < b.size(); ++j)
                    area += intersection({a[0],a[i],a[i+1]}, {b[0],b[j],b[j+1]});
            return area;
        };
        std::array<std::vector<ShakerPoint>,3> outer;
        for (size_t n = 0; n < inputs.size(); ++n) {
            outer[n] = transform(n, inputs[n].outer);
            check(scene.parts[n].angle >= -.08-1e-9 && scene.parts[n].angle <= .08+1e-9,
                  "SDK pipeline permitted continuous angle");
            for (const auto point : outer[n])
                check(point[0] >= -2e-5 && point[0] <= 60+2e-5 &&
                      point[1] >= -2e-5 && point[1] <= 40+2e-5, "SDK pipeline board containment");
        }
        const auto hole = transform(0, inputs[0].holes[0]);
        for (size_t i = 0; i < inputs.size(); ++i)
            for (size_t j = i+1; j < inputs.size(); ++j) {
                double area = overlap(outer[i], outer[j]);
                if (i == 0)
                    area -= overlap(hole, outer[j]);
                check(area < 1e-6, "SDK pipeline independent material intersection");
            }
        check(std::abs(overlap(hole, outer[1])-6.) < 1e-6,
              "SDK pipeline preserves the part nested inside the hole");
        if (trial%2)
            check(result.rotationSolves > 0 && result.cacheClears > 0,
                  "SDK pipeline exercises rotation and cache lifecycle");
        ++sdkPipelineScenes;
        ++sdkPipelineModes[size_t(geometryMode+1)];
        sdkPipelineRotations += result.rotationSolves;
        sdkPipelineCacheClears += result.cacheClears;
    }
}
