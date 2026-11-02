// Regression for projected straight spline bounds leaking Bnd_Box safety gaps.
// Run with the case 02-05-052 aligned neutral source and its pre-fix core.
#include "ExtrusionRecognition/ExtrudeRecognizesService.h"
#include "OpenCascadeResourceImport/TubeBoundaryGeometry.h"
#include "OpenCascadeResourceImport/TubeCadJson.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
#include "OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h"
#include "GeometryData/TubeNeutralPersistence.h"
#include "TemplateRuntime/StandardJsonCodec.h"
#include <BRepTools.hxx>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace G=iCAX::GeometryData;
namespace T=G::Tube;
namespace O=iCAX::OpenCascade;
namespace R=iCAX::ExtrusionRecognition;
using iCAX::Data::ObjectMap;
using iCAX::Data::VariantArray;
using Json=iCAX::TemplateRuntime::CStandardJsonCodec;

std::vector<std::uint8_t> Read(const std::filesystem::path& path)
{
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Missing regression input");
    return {(std::istreambuf_iterator<char>(input)),{}};
}

T::CPlanarRegion2 Region(const R::SSectionWiresResult& section)
{
    T::CPlanarRegion2 result;std::size_t loopIndex=0;
    for(const auto& wire:section.Wires) {
        T::SRegionLoop2 loop;loop.ID="section/loop/"+std::to_string(++loopIndex);
        std::size_t edgeIndex=0;
        for(const auto& edge:wire.Edges) {
            G::CurveSegment2 curve{edge.Curve,{edge.Range.First,edge.Range.Last,edge.bReversed}};
            if(std::holds_alternative<G::Line2>(edge.Curve)
                ||std::holds_alternative<G::Ray2>(edge.Curve)
                ||std::holds_alternative<G::Segment2>(edge.Curve)) {
                const auto support=O::BuildOpenCascadeCurve2(edge.Curve);
                const auto a=support->Value(edge.Range.First),b=support->Value(edge.Range.Last);
                curve.Curve=G::Segment2{{a.X(),a.Y()},{b.X(),b.Y()}};
                curve.Range={0.,1.,edge.bReversed};
            }
            loop.Curves.push_back({loop.ID+"/edge/"+std::to_string(++edgeIndex),std::move(curve)});
        }
        if((wire.bInner&&wire.dSignedArea>0.)||(!wire.bInner&&wire.dSignedArea<0.)) {
            std::reverse(loop.Curves.begin(),loop.Curves.end());
            for(auto& edge:loop.Curves)edge.Segment.Range.Reversed=!edge.Segment.Range.Reversed;
        }
        result.Boundaries.push_back(std::move(loop));
    }
    return result;
}

ObjectMap Gaps(const T::CPlanarRegion2& section)
{
    VariantArray gaps;double largest=0.;
    const auto endpoint=[](const G::CurveSegment2& segment,bool end) {
        const bool useLast=end!=segment.Range.Reversed;
        if(const auto line=std::get_if<G::Segment2>(&segment.Curve)) {
            const auto& point=useLast?line->End:line->Start;
            return gp_Pnt2d(point.X,point.Y);
        }
        return O::BuildOpenCascadeCurve2(segment.Curve)->Value(useLast?segment.Range.Last:segment.Range.First);
    };
    for(const auto& loop:section.Boundaries)for(std::size_t i=0;i<loop.Curves.size();++i) {
        const auto& a=loop.Curves[i].Segment;
        const auto& b=loop.Curves[(i+1)%loop.Curves.size()].Segment;
        const auto first=endpoint(a,true),last=endpoint(b,false);
        const auto gap=first.Distance(last);largest=std::max(largest,gap);
        gaps.emplace_back(ObjectMap{{"loop",loop.ID},{"edge",int(i+1)},{"gapMm",gap}});
    }
    return {{"maximumMm",largest},{"adjacentGaps",gaps}};
}

int main(int argc,char** argv)
{
    if(argc!=4)return 2;
    const std::filesystem::path output=argv[3];std::filesystem::create_directories(output);
    ObjectMap report;bool passed=false;
    try {
        const auto sourceBytes=Read(argv[1]),oldCoreBytes=Read(argv[2]);
        const auto source=G::Persistence::Deserialize(sourceBytes);
        const auto oldDocument=T::Persistence::DeserializeDocument(oldCoreBytes);
        report["before"]=Gaps(oldDocument.Geometry.Section);
        const auto built=O::ReconstructOpenCascadeShape(source);
        if(!built.bOK)throw std::runtime_error("Source reconstruction failed");
        R::CExtrudeRecognizesService recognition;
        const auto section=recognition.ExtractSectionWires(source);
        if(!section.bSuccess)throw std::runtime_error("Fresh section extraction failed");
        report["freshSection"]=iCAX::CAD::TubeCadJson::Encode(Region(section));
        T::CTubeCadDocument fresh;fresh.Revision=1;fresh.Geometry.Section=Region(section);
        report["after"]=Gaps(fresh.Geometry.Section);
        if(report.at("after").To<ObjectMap>().at("maximumMm").To<double>()>=1.e-9)
            throw std::runtime_error("Fresh exact projected contour is not closed at roundoff scale");
        R::SFeatureToolpathOptions options;options.bSingleExtrudedStock=true;
        const auto classification=recognition.ClassifySectionFaces(source,section,options);
        if(!classification.bSuccess)throw std::runtime_error("Fresh Classify failed");
        fresh.Geometry.MachiningFaces=classification.MachiningGeometry;
        report["freshMachiningFaces"]=int(fresh.Geometry.MachiningFaces.Faces.size());
        const auto restored=O::RestoreTubeBoundary(fresh.Geometry);
        const auto proof=O::ValidateTubeCSGShapeReplay(built.Shape,restored,1.e-6);
        report["classifyCoreRestorePassed"]=proof.bEquivalent;
        if(!proof.bEquivalent)throw std::runtime_error("Fresh Classify core changes source material");
        T::SExtrudedRegionNode stock;stock.Section=fresh.Geometry.Section;
        stock.Frame.XDirection={0,1,0};stock.Frame.YDirection={0,0,1};stock.Frame.ZDirection={1,0,0};
        const auto compiled=O::CompileTubeBoundary(built.Shape,stock);
        const auto whole=O::RestoreTubeBoundary(compiled.Geometry);
        report["wholeSourceCompileRestorePassed"]=O::ValidateTubeCSGShapeReplay(built.Shape,whole,1.e-6).bEquivalent;
        if(!report.at("wholeSourceCompileRestorePassed").To<bool>())throw std::runtime_error("Whole-source compile changes source material");
        report["sourceBytesUnchanged"]=sourceBytes==G::Persistence::Serialize(source)&&sourceBytes==Read(argv[1]);
        report["oldCoreBytesUnchanged"]=oldCoreBytes==Read(argv[2])&&oldCoreBytes==T::Persistence::Serialize(oldDocument);
        const auto bytes=T::Persistence::Serialize(fresh);
        const auto reopened=T::Persistence::DeserializeDocument(bytes);
        report["freshCoreBinaryRoundtripPassed"]=bytes==T::Persistence::Serialize(reopened);
        report["freshCoreReopenMaterialPassed"]=O::ValidateTubeCSGShapeReplay(built.Shape,O::RestoreTubeBoundary(reopened.Geometry),1.e-6).bEquivalent;
        for(const auto key:{"sourceBytesUnchanged","oldCoreBytesUnchanged","freshCoreBinaryRoundtripPassed","freshCoreReopenMaterialPassed"})
            if(!report.at(key).To<bool>())throw std::runtime_error("Source preservation or core persistence failed");
        BRepTools::Write(whole,(output/"whole-model.brep").string().c_str());
        std::ofstream core(output/"fresh-core.ictcad",std::ios::binary);core.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        passed=true;
    } catch(const Standard_Failure& error) {report["error"]=std::string(error.GetMessageString());}
      catch(const std::exception& error) {report["error"]=std::string(error.what());}
    report["passed"]=passed;
    const auto json=Json::Serialize(report);std::ofstream(output/"regression.json")<<json;
    std::cout<<json<<"\n";return passed?0:1;
}
