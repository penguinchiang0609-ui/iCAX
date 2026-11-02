#define CUT_UNIT_EXAMPLE_LIBRARY
#include "build_cut_unit_examples.cpp"
#include <TDF_LabelSequence.hxx>
int wmain(int argc,wchar_t** argv){try{
    if(argc!=3)return 2;
    Handle(TDocStd_Document) doc=new TDocStd_Document("BinXCAF");STEPCAFControl_Reader reader;reader.SetColorMode(true);reader.SetNameMode(true);
    if(reader.ReadFile(utf8(fs::path(argv[1])).c_str())!=IFSelect_RetDone||!reader.Transfer(doc))throw std::runtime_error("STEP-read-failed");
    auto shapes=XCAFDoc_DocumentTool::ShapeTool(doc->Main());auto colors=XCAFDoc_DocumentTool::ColorTool(doc->Main());TDF_LabelSequence roots;shapes->GetFreeShapes(roots);
    int faces=0,colored=0,invalid=0;double totalArea=0;std::ofstream o(argv[2]);o<<std::setprecision(15)<<"{\"roots\":"<<roots.Length()<<",\"entries\":[";
    for(int i=1;i<=roots.Length();++i){auto label=roots.Value(i);auto shape=shapes->GetShape(label);TopTools_IndexedMapOfShape ff;TopExp::MapShapes(shape,TopAbs_FACE,ff);int n=ff.Extent();faces+=n;Quantity_Color color;bool hasColor=colors->GetColor(label,XCAFDoc_ColorSurf,color);if(!hasColor)hasColor=colors->GetColor(label,XCAFDoc_ColorGen,color);if(hasColor)colored+=n;
        if(!hasColor){int faceColors=0;for(int j=1;j<=n;++j)if(colors->GetColor(ff(j),XCAFDoc_ColorSurf,color)||colors->GetColor(ff(j),XCAFDoc_ColorGen,color))++faceColors;colored+=faceColors;hasColor=faceColors==n&&n>0;}
        bool valid=BRepCheck_Analyzer(shape).IsValid();if(!valid)++invalid;double a=area(shape);totalArea+=a;Handle(TDataStd_Name) name;std::string text;if(label.FindAttribute(TDataStd_Name::GetID(),name)){TCollection_AsciiString ascii(name->Get(),'?');text=ascii.ToCString();}
        if(i>1)o<<',';o<<"{\"name\":\""<<text<<"\",\"faces\":"<<n<<",\"areaMm2\":"<<a<<",\"valid\":"<<(valid?"true":"false")<<",\"colored\":"<<(hasColor?"true":"false");if(hasColor){double r,g,b;color.Values(r,g,b,Quantity_TOC_sRGB);o<<",\"color\":["<<r<<','<<g<<','<<b<<']';}o<<'}';
    }o<<"],\"faces\":"<<faces<<",\"coloredFaces\":"<<colored<<",\"invalidRoots\":"<<invalid<<",\"areaMm2\":"<<totalArea<<'}';return 0;
}catch(const Standard_Failure& e){std::cerr<<e.GetMessageString();return 1;}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
