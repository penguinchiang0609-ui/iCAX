#include "recovered_escher_integer.hpp"
namespace study::recovered {
double escherIntegerScale(std::span<const ConvolutionPolygon> parts,
    std::span<const ConvolutionPolygon> sheets,int64_t limit,double gap) {
    double maximumPart=0,maximumSheet=0;
    for(const auto &part:parts) {
        const auto first=part.at(0).at(0);double x0=first[0],y0=first[1],x1=first[0],y1=first[1];
        for(const auto &ring:part)for(const auto point:ring) {
            if(!(x0<=point[0]))x0=point[0];if(!(y0<=point[1]))y0=point[1];
            if(!(point[0]<=x1))x1=point[0];if(!(point[1]<=y1))y1=point[1];
        }
        const double dy=y1-y0,dx=x1-x0;const double diagonal=std::sqrt(dy*dy+dx*dx);
        if(!(diagonal<=maximumPart))maximumPart=diagonal;
    }
    for(const auto &sheet:sheets) {
        const double largest=std::numeric_limits<double>::max();double x0=largest,y0=largest,x1=-largest,y1=-largest;
        for(const auto &ring:sheet)for(const auto point:ring) {
            if(!(x0<=point[0]))x0=point[0];if(!(y0<=point[1]))y0=point[1];
            if(!(point[0]<=x1))x1=point[0];if(!(point[1]<=y1))y1=point[1];
        }
        const double dy=(y1-y0)+gap,dx=(x1-x0)+gap;const double diagonal=std::sqrt(dy*dy+dx*dx);
        if(!(diagonal<=maximumSheet))maximumSheet=diagonal;
    }
    const double partExtent=(gap+gap)+maximumPart*4.;
    return double(limit)/(partExtent<=maximumSheet?maximumSheet:partExtent);
}
std::vector<ShakerPoint> escherIntegerReferences(std::span<const ConvolutionPolygon> polygons) {
    std::vector<ShakerPoint> references;references.reserve(polygons.size());
    for(const auto &polygon:polygons) {
        const auto first=polygon.at(0).at(0);double x0=first[0],y0=first[1],x1=first[0],y1=first[1];
        for(const auto &ring:polygon)for(const auto point:ring) {
            if(!(x0<=point[0]))x0=point[0];if(!(y0<=point[1]))y0=point[1];
            if(!(point[0]<=x1))x1=point[0];if(!(point[1]<=y1))y1=point[1];
        }
        references.push_back({(x1+x0)*.5,(y1+y0)*.5});
    }
    return references;
}
EscherIntegerGeometry escherIntegerGeometry(std::span<const ConvolutionPolygon> parts,
    std::span<const ConvolutionPolygon> sheets,int64_t limit,double gap) {
    EscherIntegerGeometry result;result.scale=escherIntegerScale(parts,sheets,limit,gap);
    result.partReferences=escherIntegerReferences(parts);result.sheetReferences=escherIntegerReferences(sheets);
    const auto convert=[&](std::span<const ConvolutionPolygon> input,std::span<const ShakerPoint> references) {
        std::vector<ConvolutionIntegerPolygon> converted;converted.reserve(input.size());
        for(size_t index=0;index<input.size();++index) {
            auto &polygon=converted.emplace_back();polygon.reserve(input[index].size());
            for(const auto &ring:input[index]) {
                auto scaled=ring;for(auto &point:scaled) {
                    point[0]=(point[0]-references[index][0])*result.scale;
                    point[1]=(point[1]-references[index][1])*result.scale;
                }
                polygon.push_back(convolutionRoundRing(scaled));
            }
        }
        return converted;
    };
    result.parts=convert(parts,result.partReferences);result.sheets=convert(sheets,result.sheetReferences);return result;
}
}
