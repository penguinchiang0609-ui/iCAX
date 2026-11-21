#include "recovered_shaker_finalize.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
namespace study::recovered {
namespace {
constexpr double infinity=std::numeric_limits<double>::infinity();
double neg(double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)^0x8000000000000000ULL);}
ShakerPoint normalize(ShakerPoint v){const double length=std::sqrt(v[1]*v[1]+v[0]*v[0]);if(1e-9>length)return {1,0};const double scale=1/length;return {v[0]*scale,v[1]*scale};}
void requireDirections(const ShakerProblem &p){if(!p.vector60.valid||!p.vector78.valid)throw std::invalid_argument("Missing Shaker finalizer direction vectors");}
}
void shakerSpecialLineRow(LinearModel &model,std::array<int32_t,2> columns,ShakerPoint a,ShakerPoint b){
    const double dx=b[0]-a[0],dy=b[1]-a[1],length=std::sqrt(dx*dx+dy*dy);const auto n=1e-9>length?ShakerPoint{1,0}:ShakerPoint{(1/length)*dx,(1/length)*dy};
    model.addRow({{columns[0],n[1]},{columns[1],neg(n[0])}},-infinity,a[0]*n[1]-a[1]*n[0]);
}
void shakerSpecialDirectionRows(LinearModel &model,std::array<int32_t,2> columns,ShakerPoint position,ShakerPoint reference,ShakerPoint direction){
    const ShakerPoint delta{position[0]-reference[0],position[1]-reference[1]},n=normalize(delta),d=normalize(direction);
    const double dot=d[1]*n[1]+d[0]*n[0],dy=neg(direction[1]),length=std::sqrt(dy*dy+direction[0]*direction[0]);
    const ShakerPoint line=length!=0?ShakerPoint{dy/length,direction[0]/length}:ShakerPoint{1,0};
    const double parameter=(reference[1]-position[1])*line[1]+(reference[0]-position[0])*line[0];
    ShakerPoint projected{line[0]*parameter+position[0],line[1]*parameter+position[1]};
    const double epsilon=std::bit_cast<double>(0x3ddb7cdfd9d7bdbbULL);
    if(!(epsilon>std::abs(dot))){
        const double cross=(projected[1]-reference[1])*delta[0]-(projected[0]-reference[0])*delta[1];
        if(cross>0){shakerSpecialLineRow(model,columns,reference,position);shakerSpecialLineRow(model,columns,position,projected);shakerSpecialLineRow(model,columns,projected,reference);}
        else{shakerSpecialLineRow(model,columns,reference,projected);shakerSpecialLineRow(model,columns,projected,position);shakerSpecialLineRow(model,columns,position,reference);}
    }else{
        const double cross=((reference[1]+d[1])-reference[1])*delta[0]-((reference[0]+d[0])-reference[0])*delta[1];
        projected=cross<0?ShakerPoint{reference[0]+d[0],reference[1]+d[1]}:ShakerPoint{reference[0]-d[0],reference[1]-d[1]};
        shakerSpecialLineRow(model,columns,reference,projected);
    }
}
size_t shakerFilterDeferred(std::vector<DeferredShakerRow> &rows,ShakerPoint direction){
    const auto old=rows.size();const double epsilon=std::bit_cast<double>(0x3ddb7cdfd9d7bdbbULL);
    rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const auto &row){const auto n=normalize({row.terms[0].coefficient,row.terms[1].coefficient});return epsilon>std::abs(n[0]*direction[0]+n[1]*direction[1]);}),rows.end());return old-rows.size();
}
std::map<int32_t,std::array<uint64_t,2>> shakerSpecialPartRows(LinearModel &model,std::array<int32_t,2> columns,int32_t auxiliary,
    std::vector<DeferredShakerRow> &rows,ShakerPoint position,ShakerPoint reference,std::optional<ShakerPoint> direction){
    const auto n=normalize({position[0]-reference[0],position[1]-reference[1]});const double rhs=n[0]*reference[0]+n[1]*reference[1];
    model.addRow({{columns[0],n[0]},{columns[1],n[1]},{auxiliary,-1}},rhs,rhs);
    if(direction)shakerSpecialDirectionRows(model,columns,position,reference,*direction);
    else{shakerFilterDeferred(rows,n);const double perpendicular=n[1]*reference[0]-n[0]*reference[1];model.addRow({{columns[0],n[1]},{columns[1],neg(n[0])}},perpendicular,perpendicular);}
    std::map<int32_t,std::array<uint64_t,2>> result;
    for(const auto &row:rows){const int32_t id=model.addLowerRow({row.terms[0],row.terms[1]},row.lower);result[id]={std::bit_cast<uint64_t>(row.terms[0].coefficient),std::bit_cast<uint64_t>(row.terms[1].coefficient)};row.first->rows.push_back(id);row.second->rows.push_back(id);}
    return result;
}
void shakerDirectionalCornerRows(const ShakerProblem &p,LinearModel &model,bool first,bool second){
    requireDirections(p);const auto a=normalize(p.vector60.point),b=normalize(p.vector78.point);const double determinant=1/(b[1]*a[0]-a[1]*b[0]);
    double x=determinant*(second?b[1]:neg(a[1])),y=determinant*(second?neg(b[0]):a[0]);
    const auto &v=first?p.vector60.point:p.vector78.point;double rhs=std::sqrt(v[1]*v[1]+v[0]*v[0]);if(first!=second)rhs=0;
    const size_t offset=first?0:2;const auto corner=p.auxiliaryEC[(first?0:2)+(second?0:1)];
    model.addLowerRow({{corner,1},{p.auxiliaryDC[offset],x},{p.auxiliaryDC[offset+1],y}},rhs);
    model.addLowerRow({{corner,1},{p.auxiliaryDC[offset],neg(x)},{p.auxiliaryDC[offset+1],neg(y)}},neg(rhs));
}
void shakerFinalizeModel(ShakerProblem &p,std::span<ShakerPart> parts,LinearModel &model,const ShakerFinalizeState &state,ShakerReportState &report,ShakerSceneGeometry &scene){
    if(p.policy3)for(size_t n=0;n<parts.size();++n)if(parts[n].special88){const auto &part=parts[n];p.specialRows=shakerSpecialPartRows(model,{part.columns[0],part.columns[1]},p.auxiliaryD8,p.deferred,part.position,state.specialReferences.at(n),state.specialDirection);}
    if(!p.free5A)return;
    for(auto index:state.freeParts){if(index<0||size_t(index)>=parts.size())throw std::out_of_range("Invalid Shaker free part");auto &part=parts[size_t(index)];const auto position=part.position;part.position={0,0};part.geometryValid30=0;shakerRefreshPart(parts,scene,size_t(index));requireDirections(p);
        const auto bounds=shakerPartProjection(parts,scene,size_t(index),p.vector60.point,p.vector78.point).value;
        part.position=position;part.geometryValid30=0;shakerRefreshPart(parts,scene,size_t(index));requireDirections(p);
        auto a=p.vector60.point,b=p.vector78.point;const double al=std::sqrt(a[1]*a[1]+a[0]*a[0]),bl=std::sqrt(b[1]*b[1]+b[0]*b[0]);a={a[0]/al,a[1]/al};b={b[0]/bl,b[1]/bl};const double determinant=1/(b[1]*a[0]-a[1]*b[0]);
        const double bx=determinant*b[1],by=neg(b[0])*determinant,ax=determinant*a[1],ay=neg(determinant*a[0]);
        model.addLowerRow({{p.auxiliary114[1],1},{part.columns[0],ax},{part.columns[1],ay}},bounds[3]);
        model.addRow({{p.auxiliary114[0],1},{part.columns[0],ax},{part.columns[1],ay}},-infinity,bounds[1]);
        model.addLowerRow({{p.auxiliary114[3],1},{part.columns[0],neg(bx)},{part.columns[1],neg(by)}},bounds[2]);
        model.addRow({{p.auxiliary114[2],1},{part.columns[0],neg(bx)},{part.columns[1],neg(by)}},-infinity,bounds[0]);
    }
    requireDirections(p);for(bool first:{false,true})for(bool second:{false,true})shakerDirectionalCornerRows(p,model,first,second);
    report.directionalRows[0]=model.addRow({{p.auxiliary114[1],1},{p.auxiliary114[0],-1},{p.auxiliaryEC[1],double(state.directionMultiplicities[0])},{p.auxiliaryEC[3],double(state.directionMultiplicities[1])}},-infinity,(state.ranges[1]-state.ranges[0])+state.allowances[0]);
    report.directionalRows[1]=model.addRow({{p.auxiliary114[3],1},{p.auxiliary114[2],-1},{p.auxiliaryEC[0],double(state.directionMultiplicities[0])},{p.auxiliaryEC[2],double(state.directionMultiplicities[1])}},-infinity,(state.ranges[3]-state.ranges[2])+state.allowances[1]);
}
}
