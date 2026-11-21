#include "recovered_escher_toroidal.hpp"
#include "recovered_escher_candidate.hpp"
#include "clipper2/clipper.h"
#include <iostream>
using namespace study;using namespace study::recovered;
int main(){try{size_t scenes=0,solves=0,improved=0;
    for(size_t trial=0;trial<3;++trial){EscherIntegerShapeRecord owner;owner.shapeId=1;owner.quantity=20;owner.metadata4=0;
        ConvolutionIntegerPolygon polygons=trial==1?ConvolutionIntegerPolygon{{{-30,-30},{30,-30},{30,-10},{-10,-10},{-10,30},{-30,30}}}:ConvolutionIntegerPolygon{{{-25,-15},{25,-15},{25,15},{-25,15}}};
        if(trial==2)polygons.push_back({{-10,-5},{-10,5},{10,5},{10,-5}});
        owner.normal=EscherIntegerShapeOwner(EscherNormalizedContours{polygons});auto catalog=escherProduceGeometryCatalog(owner,8);const auto before=catalog.ordinary.size();Budget budget(20.);EscherToroidalTrace trace;
        escherImproveToroidalCatalog(catalog,1,&budget,&trace);require(trace.candidates>0&&trace.solves>0,PN2S_INTERNAL,"Real continuous Toroidal geometry never entered its specialized LP");
        std::cout<<"{\"scene\":"<<trial<<",\"initialDescriptors\":"<<before<<",\"finalDescriptors\":"<<catalog.ordinary.size()<<",\"candidates\":"<<trace.candidates<<",\"solves\":"<<trace.solves<<",\"acceptedSteps\":"<<trace.acceptedSteps<<",\"failedSolves\":"<<trace.failedSolves<<",\"improved\":"<<trace.improved<<"}\n";
        if(trial==0){auto source=escherSourceDescriptor(std::array{escherOwnerPatternRecord(owner,0,0)});
            auto candidate=escherNormalizePeriodBasis({EscherIntegerPoint{51,0},EscherIntegerPoint{0,31}});candidate.boxes=escherCandidateBoxes(source.supports,candidate.basis[0]);candidate.offsets.resize(1);escherNormalizeCandidatePhase(candidate);
            int64_t minimum=1581;EscherToroidalTrace perturbed;auto result=escherToroidalShakeModel(source.descriptor,candidate,minimum,&budget,&perturbed);
            require(result&&perturbed.acceptedSteps>0,PN2S_INTERNAL,"Real Toroidal LP did not improve a valid loose rectangular lattice");
            require(minimum==1500,PN2S_INTERNAL,"Toroidal actual material lattice did not reach its50x30 area");
            const auto &periods=result->normalCandidates.empty()?result->specialCandidates:result->normalCandidates;require(!periods.empty(),PN2S_INTERNAL,"Toroidal result omitted its actual lattice");
            std::vector<Clipper2Lib::PathD> placed;double overlap=0.;for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(const auto &record:result->records){Clipper2Lib::PathD path;const double radians=double(record.angle)/10000.*pi/180.,c=std::cos(radians),s=std::sin(radians);for(const auto p:polygons.front()){double x=double(p[0])*c-double(p[1])*s,y=double(p[0])*s+double(p[1])*c;if(uint8_t(record.reflected))y=-y;path.push_back({x+record.x+i*periods.front().basis[0][0]+j*periods.front().basis[1][0],y+record.y+i*periods.front().basis[0][1]+j*periods.front().basis[1][1]});}for(const auto &prior:placed)overlap=std::max(overlap,std::fabs(Clipper2Lib::Area(Clipper2Lib::Intersect({path},{prior},Clipper2Lib::FillRule::NonZero,8))));placed.push_back(std::move(path));}
            require(overlap==0.,PN2S_INTERNAL,"Independent repeated material geometry overlaps after specialized Toroidal LP");
            std::cout<<"{\"looseLatticeAcceptedSteps\":"<<perturbed.acceptedSteps<<",\"solves\":"<<perturbed.solves<<",\"failed\":"<<perturbed.failedSolves<<",\"minimumCellArea\":"<<minimum<<",\"independentRepeatedPlacements\":"<<placed.size()<<",\"maximumOverlapArea\":"<<overlap<<"}\n";
        }
        ++scenes;solves+=trace.solves;improved+=trace.improved;
    }
    std::cout<<"{\"pass\":true,\"scenes\":"<<scenes<<",\"specializedSolves\":"<<solves<<",\"improvedDescriptors\":"<<improved<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
