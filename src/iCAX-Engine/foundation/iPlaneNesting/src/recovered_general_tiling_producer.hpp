#pragma once
#include "recovered_amo_producer.hpp"
#include "recovered_escher_provider.hpp"
namespace study::recovered {
struct GeneralTilingSheet {
    double width=0,height=0,left=0,right=0,bottom=0,top=0;
    Point usable()const{return {(width-left)-right,(height-bottom)-top};}
};
struct GeneralTilingControlInput {
    GeneralTilingSheet sheet;
    std::vector<PlacementOrientation> orientations;
    bool symmetryX=false,symmetryY=false,preferReflection228=false,expand=false,optional=false;
    double value=0,gap=0,templateLimit678=0,layoutTrials688=0,seed178=0;
};
struct GeneralTilingControlState {
    std::vector<std::shared_ptr<TilingPattern>> models;
    std::vector<TilingCandidate> expanded;
};
class GeneralTilingControlHost {
public:
    virtual ~GeneralTilingControlHost()=default;
    virtual bool zeroVariantAvailable()=0; //54ec10, before stop check
    virtual bool stopped()=0;
    virtual std::vector<AmoTilingPatternResult> orientationPatterns(PlacementOrientation,PlacementOrientation)=0; //4a4e00
    virtual bool hasSupplementalProvider()=0; //3879e0
    virtual void addSupplementalPattern(const std::shared_ptr<TilingPattern> &)=0; //494a20
    // Returned templates retain the original ordered tree traversal.
    virtual std::vector<std::shared_ptr<TilingPattern>> supplementalTemplates(int32_t,Point usable)=0; //4a1d80
    virtual std::vector<TilingMember> supplementalLayout(const EscherProviderRequest &)=0; //4943e0 /493b00+4939a0
};
TilingPattern generalTilingLayoutPattern(std::vector<TilingMember>,double gap); //49cec0
void generalTilingProduce(GeneralTilingControlState &,const GeneralTilingControlInput &,
    GeneralTilingControlHost &); //4a0800
}
