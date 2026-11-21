#include "recovered_amo_shape.hpp"
namespace study::recovered {
void amoTilingComposeMatrix(AmoTilingMatrix &a,const AmoTilingMatrix &b){
    const std::array<double,4> scaled{a[0]*a[6],a[1]*a[6],a[2]*a[6],a[3]*a[6]};
    a[4]=b[5]*scaled[1]+b[4]*scaled[0]+a[4];a[5]=b[5]*scaled[3]+b[4]*scaled[2]+a[5];
    const double first=a[0];a[0]=a[1]*b[2]+first*b[0];a[1]=a[1]*b[3]+first*b[1];
    const double last=a[3];a[3]=last*b[3]+a[2]*b[1];a[2]=last*b[2]+a[2]*b[0];a[6]=b[6]*a[6];
}
Point amoTilingTransformPoint(Point p,const AmoTilingMatrix &m){
    return {p.y*(m[1]*m[6])+p.x*(m[0]*m[6])+m[4],p.y*(m[3]*m[6])+p.x*(m[2]*m[6])+m[5]};
}
AmoTilingMatrix amoTilingReverseMatrix(double angle){
    const double radians=(angle*3.141592653589793)/180.,x=std::cos(radians),y=std::sin(radians),norm=std::sqrt((-y)*(-y)+x*x);
    AmoTilingMatrix matrix{x,norm!=0.?(-y)/norm:1.,y,norm!=0.?x/norm:0.,0.,0.,1.};
    amoTilingComposeMatrix(matrix,{1.,0.,0.,-1.,0.,0.,1.});amoTilingComposeMatrix(matrix,{-1.,0.,0.,1.,0.,0.,1.});return matrix;
}
AmoTilingTransformedShape amoTilingTransformShape(const AmoTilingShape &source,const AmoTilingMatrix &matrix,const AmoTilingShapeProperties &properties){
    AmoTilingTransformedShape result;result.properties=properties;result.properties.flaggedPoint={};
    const bool reversed=matrix[3]*matrix[0]-matrix[2]*matrix[1]<0.;
    for(const auto &ring:source.rings){auto points=ring;for(auto &p:points)p=amoTilingTransformPoint(p,matrix);if(reversed)std::reverse(points.begin(),points.end());
        for(const auto p:points){auto &bounds=result.geometry.bounds;if(bounds.empty){bounds.empty=false;bounds.box={p.x,p.y,p.x,p.y};}else{if(p.x<bounds.box.x0)bounds.box.x0=p.x;if(p.y<bounds.box.y0)bounds.box.y0=p.y;if(bounds.box.x1<p.x)bounds.box.x1=p.x;if(bounds.box.y1<p.y)bounds.box.y1=p.y;}}
        result.geometry.rings.push_back(std::move(points));
    }
    if(properties.flag5a)result.properties.flaggedPoint=amoTilingTransformPoint(properties.flaggedPoint,matrix);return result;
}
}
