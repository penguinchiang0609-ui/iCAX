#include "GeometryServices/PlaneArtworkService.h"
#include "PlaneGeometryAlgorithms.h"
#include "Services/ServicesHelper.h"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace {
namespace G = iCAX::GeometryServices;
namespace A = G::PlaneGeometryDetail;
using Microsoft::WRL::ComPtr;
using Map = G::PlaneGeometryData::Map;
using Array = G::PlaneGeometryData::Array;
using Text = G::PlaneGeometryData::Text;
using Point = G::PlaneNesting::Point;
using Ring = G::PlaneNesting::Ring;
constexpr std::size_t maxPixels = 16 * 1024 * 1024, maxBytes = 64 * 1024 * 1024;
void Require(HRESULT status) { if (FAILED(status)) throw std::invalid_argument("PLANE_ARTWORK_IMAGE_CODEC_FAILED"); }
struct ComScope {
  HRESULT Status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ComScope() { if (FAILED(Status) && Status != RPC_E_CHANGED_MODE) Require(Status); }
  ~ComScope() { if (SUCCEEDED(Status)) CoUninitialize(); }
};
struct Pixels { UINT Width = 0, Height = 0;std::vector<std::uint8_t> RGBA; };
void CheckSize(UINT width, UINT height) {
  if (!width || !height || width > 16384 || height > 16384 || static_cast<std::uint64_t>(width) * height > maxPixels)
    throw std::invalid_argument("PLANE_ARTWORK_IMAGE_SIZE_LIMIT");
}
ComPtr<IWICImagingFactory> Factory() {
  ComPtr<IWICImagingFactory> factory;Require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));return factory;
}
Pixels Decode(IWICImagingFactory* factory, const std::vector<std::uint8_t>& bytes) {
  if (bytes.empty() || bytes.size() > maxBytes) throw std::invalid_argument("PLANE_ARTWORK_IMAGE_FILE_SIZE_LIMIT");
  ComPtr<IWICStream> stream;Require(factory->CreateStream(&stream));
  Require(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())));
  ComPtr<IWICBitmapDecoder> decoder;Require(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder));
  ComPtr<IWICBitmapFrameDecode> frame;Require(decoder->GetFrame(0, &frame));
  Pixels pixels;Require(frame->GetSize(&pixels.Width, &pixels.Height));CheckSize(pixels.Width, pixels.Height);
  ComPtr<IWICFormatConverter> converter;Require(factory->CreateFormatConverter(&converter));
  Require(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
  pixels.RGBA.resize(static_cast<std::size_t>(pixels.Width) * pixels.Height * 4);
  Require(converter->CopyPixels(nullptr, pixels.Width * 4, static_cast<UINT>(pixels.RGBA.size()), pixels.RGBA.data()));return pixels;
}
std::vector<std::uint8_t> Encode(IWICImagingFactory* factory, const Pixels& pixels) {
  ComPtr<IStream> stream;Require(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
  ComPtr<IWICBitmapEncoder> encoder;Require(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));Require(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
  ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> properties;Require(encoder->CreateNewFrame(&frame, &properties));Require(frame->Initialize(properties.Get()));
  Require(frame->SetSize(pixels.Width, pixels.Height));WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;Require(frame->SetPixelFormat(&format));
  ComPtr<IWICBitmap> bitmap;Require(factory->CreateBitmapFromMemory(pixels.Width, pixels.Height, GUID_WICPixelFormat32bppRGBA, pixels.Width * 4,
    static_cast<UINT>(pixels.RGBA.size()), const_cast<BYTE*>(pixels.RGBA.data()), &bitmap));
  ComPtr<IWICFormatConverter> converter;Require(factory->CreateFormatConverter(&converter));
  Require(converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
  Require(frame->WriteSource(converter.Get(), nullptr));Require(frame->Commit());Require(encoder->Commit());
  STATSTG stat{};Require(stream->Stat(&stat, STATFLAG_NONAME));
  if (stat.cbSize.QuadPart > maxBytes) throw std::invalid_argument("PLANE_ARTWORK_IMAGE_FILE_SIZE_LIMIT");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));LARGE_INTEGER zero{};Require(stream->Seek(zero, STREAM_SEEK_SET, nullptr));
  ULONG read = 0;Require(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read));if (read != bytes.size()) throw std::runtime_error("PLANE_ARTWORK_IMAGE_READ_FAILED");return bytes;
}
std::wstring Wide(const Text& text) {
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (!length) throw std::invalid_argument("PLANE_ARTWORK_INVALID_TEXT");
  std::wstring value(length, 0);MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), value.data(), length);return value;
}
G::PlaneArtworkBitmap Normalize(const std::vector<std::uint8_t>& source) {
  ComScope scope;const auto factory = Factory();const auto pixels = Decode(factory.Get(), source);
  return {Encode(factory.Get(), pixels), pixels.Width, pixels.Height, pixels.Width * 25.4 / 96., pixels.Height * 25.4 / 96.};
}
G::PlaneArtworkBitmap TextBitmap(const Map& options) {
  const auto text = A::String(options, "text"), family = A::String(options, "fontFamily", "Microsoft YaHei");
  const double size = A::Number(options, "fontSizeMm", 10), pixelsPerMm = 8;
  if (text.empty() || text.size() > 32768 || family.empty() || family.size() > 256 || !(size > 0) || size > 512)
    throw std::invalid_argument("PLANE_ARTWORK_INVALID_TEXT");
  const auto value = Wide(text), fontFamily = Wide(family);
  struct Surface {
    HDC DC = CreateCompatibleDC(nullptr);HFONT Font = nullptr;HBITMAP Bitmap = nullptr;HGDIOBJ OldFont = nullptr, OldBitmap = nullptr;
    ~Surface() { if (OldBitmap) SelectObject(DC, OldBitmap);if (OldFont) SelectObject(DC, OldFont);if (Bitmap) DeleteObject(Bitmap);if (Font) DeleteObject(Font);if (DC) DeleteDC(DC); }
  } surface;
  if (!surface.DC) throw std::runtime_error("PLANE_ARTWORK_FONT_FAILED");
  surface.Font = CreateFontW(-static_cast<int>(std::ceil(size * pixelsPerMm)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFamily.c_str());
  if (!surface.Font) throw std::runtime_error("PLANE_ARTWORK_FONT_FAILED");
  surface.OldFont = SelectObject(surface.DC, surface.Font);RECT bounds{0,0,0,0};
  if (!DrawTextW(surface.DC, value.data(), static_cast<int>(value.size()), &bounds, DT_CALCRECT | DT_NOPREFIX | DT_EXPANDTABS))
    throw std::invalid_argument("PLANE_ARTWORK_TEXT_HAS_NO_DISPLAY");
  Pixels pixels;pixels.Width = std::max(1L, bounds.right - bounds.left) + 4;pixels.Height = std::max(1L, bounds.bottom - bounds.top) + 4;CheckSize(pixels.Width, pixels.Height);
  BITMAPINFO info{};info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth = pixels.Width;info.bmiHeader.biHeight = -static_cast<LONG>(pixels.Height);
  info.bmiHeader.biPlanes = 1;info.bmiHeader.biBitCount = 32;info.bmiHeader.biCompression = BI_RGB;
  void* raw = nullptr;surface.Bitmap = CreateDIBSection(surface.DC, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
  if (!surface.Bitmap || !raw) throw std::runtime_error("PLANE_ARTWORK_FONT_FAILED");
  surface.OldBitmap = SelectObject(surface.DC, surface.Bitmap);std::memset(raw, 0, static_cast<std::size_t>(pixels.Width) * pixels.Height * 4);
  SetBkMode(surface.DC, TRANSPARENT);SetTextColor(surface.DC, RGB(255,255,255));RECT target{2,2,static_cast<LONG>(pixels.Width)-2,static_cast<LONG>(pixels.Height)-2};
  if (!DrawTextW(surface.DC, value.data(), static_cast<int>(value.size()), &target, DT_NOPREFIX | DT_EXPANDTABS)) throw std::runtime_error("PLANE_ARTWORK_FONT_FAILED");
  GdiFlush();pixels.RGBA.resize(static_cast<std::size_t>(pixels.Width) * pixels.Height * 4);const auto source = static_cast<const std::uint8_t*>(raw);
  for (std::size_t i = 0; i < pixels.RGBA.size(); i += 4) {
    pixels.RGBA[i] = pixels.RGBA[i+1] = pixels.RGBA[i+2] = 255;pixels.RGBA[i+3] = std::max({source[i],source[i+1],source[i+2]});
  }
  ComScope scope;const auto factory = Factory();return {Encode(factory.Get(), pixels), pixels.Width, pixels.Height, pixels.Width / pixelsPerMm, pixels.Height / pixelsPerMm};
}
double Area(const Ring& ring) { double result = 0;for (std::size_t i=0;i<ring.size();++i) {const auto a=ring[i], b=ring[(i+1)%ring.size()];result+=a.X*b.Y-a.Y*b.X;}return result/2; }
double SegmentDistance(Point p, Point a, Point b) {
  const double dx=b.X-a.X,dy=b.Y-a.Y,length=dx*dx+dy*dy,t=length?std::clamp(((p.X-a.X)*dx+(p.Y-a.Y)*dy)/length,0.,1.):0.;return std::hypot(p.X-a.X-t*dx,p.Y-a.Y-t*dy);
}
Ring Simplify(const Ring& ring, double tolerance, const std::function<bool()>& cancelled) {
  if (!(tolerance>0) || ring.size()<5) return ring;
  Ring points=ring;points.push_back(ring.front());std::vector<bool> keep(points.size());keep.front()=keep.back()=true;
  std::vector<std::pair<std::size_t,std::size_t>> stack{{0,points.size()-1}};
  std::size_t work = 0;
  while (!stack.empty()) {
    if(cancelled&&cancelled())throw std::runtime_error("PLANE_GEOMETRY_CANCELLED");
    const auto [first,last]=stack.back();stack.pop_back();double maximum=tolerance;std::size_t selected=first;
    for (std::size_t i=first+1;i<last;++i) {if(++work>50000000)throw std::invalid_argument("PLANE_ARTWORK_TRACE_COMPLEXITY_LIMIT");const double distance=SegmentDistance(points[i],points[first],points[last]);if(distance>maximum){maximum=distance;selected=i;}}
    if(selected!=first){keep[selected]=true;stack.emplace_back(first,selected);stack.emplace_back(selected,last);}
  }
  Ring result;for(std::size_t i=0;i<ring.size();++i)if(keep[i])result.push_back(ring[i]);return result.size()>=3?result:ring;
}
Array CurveRecords(const std::vector<Ring>& rings) {
  Array curves;for(const auto& ring:rings){Array points;for(const auto p:ring)points.emplace_back(Array{p.X,p.Y});
    curves.emplace_back(Map{{"type",Text("polyline")},{"points",points},{"closed",true}});}return curves;
}
bool SameRings(const std::vector<Ring>& a,const std::vector<Ring>& b) {
  if(a.size()!=b.size())return false;
  for(std::size_t i=0;i<a.size();++i){if(a[i].size()!=b[i].size())return false;
    for(std::size_t j=0;j<a[i].size();++j)if(a[i][j].X!=b[i][j].X||a[i][j].Y!=b[i][j].Y)return false;}return true;
}
bool SafeSimplification(const std::vector<Ring>& original,const std::vector<Ring>& candidate,const std::function<bool()>& cancelled) {
  // Reuse the exact OCCT intersection checker instead of accepting the RDP
  // distance bound as a topology guarantee. Over-budget candidates keep the
  // already valid pixel boundary; no large quadratic verification is started.
  std::size_t edges=0;for(const auto& ring:candidate)edges+=ring.size();if(edges>2048)return false;
  try{A::ValidateClosedCurveIntersections(CurveRecords(candidate),cancelled);}
  catch(const std::exception& error){if(Text(error.what()).find("PLANE_GEOMETRY_CANCELLED")!=Text::npos)throw;return false;}
  struct Bounds{double X0=std::numeric_limits<double>::max(),Y0=X0,X1=-X0,Y1=-X0;};
  const auto bounds=[](const Ring& ring){Bounds b;for(const auto p:ring){b.X0=std::min(b.X0,p.X);b.Y0=std::min(b.Y0,p.Y);b.X1=std::max(b.X1,p.X);b.Y1=std::max(b.Y1,p.Y);}return b;};
  std::vector<Bounds> before,after;for(std::size_t i=0;i<candidate.size();++i){
    if(Area(original[i])*Area(candidate[i])<=0)return false;
    before.push_back(bounds(original[i]));after.push_back(bounds(candidate[i]));}
  std::size_t work=0;bool exhausted=false;
  const auto inside=[&](Point p,const Ring& ring,const Bounds& b){
    if(p.X<b.X0||p.Y<b.Y0||p.X>b.X1||p.Y>b.Y1)return false;
    bool value=false;for(std::size_t i=0,j=ring.size()-1;i<ring.size();j=i++){
      if(++work>2000000){exhausted=true;return false;}
      const auto a=ring[i],c=ring[j];if((a.Y>p.Y)!=(c.Y>p.Y)&&p.X<(c.X-a.X)*(p.Y-a.Y)/(c.Y-a.Y)+a.X)value=!value;
    }return value;
  };
  // Disjoint simple candidates can still cut an entire hole out of its outer
  // loop. Preserve every original containment relation as well as direction.
  for(std::size_t i=0;i<candidate.size();++i){if(cancelled&&cancelled())throw std::runtime_error("PLANE_GEOMETRY_CANCELLED");
    for(std::size_t j=0;j<candidate.size();++j)if(i!=j){
      const bool wasInside=inside(original[i].front(),original[j],before[j]);
      const bool nowInside=inside(candidate[i].front(),candidate[j],after[j]);
      if(exhausted||wasInside!=nowInside)return false;
    }}return true;
}
Map Trace(const std::vector<std::uint8_t>& source, const Map& options, const std::function<bool()>& cancelled) {
  const auto cancel=[&]{if(cancelled&&cancelled())throw std::runtime_error("PLANE_GEOMETRY_CANCELLED");};cancel();
  ComScope scope;const auto factory=Factory();const auto pixels=Decode(factory.Get(),source);
  const double width=A::Number(options,"widthMm",pixels.Width*25.4/96.),height=A::Number(options,"heightMm",pixels.Height*25.4/96.);
  const double threshold=A::Number(options,"threshold",128), minimum=A::Number(options,"minAreaMm2"), simplify=A::Number(options,"simplifyMm",.1);
  if(!(width>0)||!(height>0)||threshold<0||threshold>255||minimum<0||simplify<0||simplify>std::max(width,height))throw std::invalid_argument("PLANE_ARTWORK_INVALID_TRACE_OPTIONS");
  const bool invert=options.contains("invert")&&options.at("invert").To<bool>();
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(pixels.Width)*pixels.Height);
  for(UINT y=0;y<pixels.Height;++y){cancel();for(UINT x=0;x<pixels.Width;++x){const auto i=(static_cast<std::size_t>(y)*pixels.Width+x)*4;
    const auto alpha=pixels.RGBA[i+3];const double luminance=(54.*pixels.RGBA[i]+183.*pixels.RGBA[i+1]+19.*pixels.RGBA[i+2])/256.;
    const double composite=(luminance*alpha+255.*(255-alpha))/255.;mask[i/4]=alpha>=16&&((composite<threshold)!=invert);}}
  struct Edge {std::uint64_t A,B;int Direction;bool Used=false;};std::vector<Edge> edges;
  std::unordered_map<std::uint64_t,std::vector<std::size_t>> outgoing;
  const auto key=[&](UINT x,UINT y){return static_cast<std::uint64_t>(y)*(pixels.Width+1ull)+x;};
  const auto occupied=[&](int x,int y){return x>=0&&y>=0&&x<static_cast<int>(pixels.Width)&&y<static_cast<int>(pixels.Height)&&mask[static_cast<std::size_t>(y)*pixels.Width+x];};
  const auto edge=[&](UINT x1,UINT y1,UINT x2,UINT y2,int direction){if(edges.size()>=1000000)throw std::invalid_argument("PLANE_ARTWORK_TRACE_COMPLEXITY_LIMIT");const auto a=key(x1,y1);outgoing[a].push_back(edges.size());edges.push_back({a,key(x2,y2),direction});};
  for(UINT y=0;y<pixels.Height;++y){cancel();for(UINT x=0;x<pixels.Width;++x)if(occupied(x,y)){
    const int ix=static_cast<int>(x),iy=static_cast<int>(y);
    if(!occupied(ix,iy-1))edge(x,y,x+1,y,0);if(!occupied(ix+1,iy))edge(x+1,y,x+1,y+1,1);
    if(!occupied(ix,iy+1))edge(x+1,y+1,x,y+1,2);if(!occupied(ix-1,iy))edge(x,y+1,x,y,3);}}
  std::vector<Ring> original,candidate;std::size_t filtered=0;
  for(std::size_t first=0;first<edges.size();++first){if(edges[first].Used)continue;cancel();Ring ring;std::size_t current=first;
    for(std::size_t work=0;work<=edges.size();++work){cancel();auto& e=edges[current];if(e.Used)throw std::runtime_error("PLANE_ARTWORK_TRACE_TOPOLOGY_FAILED");e.Used=true;
      ring.push_back({static_cast<double>(e.A%(pixels.Width+1ull))*width/pixels.Width,(pixels.Height-static_cast<double>(e.A/(pixels.Width+1ull)))*height/pixels.Height});
      if(e.B==edges[first].A)break;auto found=outgoing.find(e.B);if(found==outgoing.end())throw std::runtime_error("PLANE_ARTWORK_TRACE_OPEN_CONTOUR");
      std::size_t next=edges.size();int priority=5;for(auto candidate:found->second)if(!edges[candidate].Used){const int turn=(edges[candidate].Direction-e.Direction+4)%4;const int score=turn==1?0:turn==0?1:turn==3?2:3;if(score<priority){priority=score;next=candidate;}}
      if(next==edges.size())throw std::runtime_error("PLANE_ARTWORK_TRACE_OPEN_CONTOUR");current=next;
    }
    Ring corners;for(std::size_t i=0;i<ring.size();++i){const auto a=ring[(i+ring.size()-1)%ring.size()],b=ring[i],c=ring[(i+1)%ring.size()];if((b.X-a.X)*(c.Y-b.Y)!=(b.Y-a.Y)*(c.X-b.X))corners.push_back(b);}
    if(corners.size()<3||std::abs(Area(corners))<minimum){++filtered;continue;}
    candidate.push_back(Simplify(corners,simplify,cancelled));original.push_back(std::move(corners));
    if(original.size()>4096)throw std::invalid_argument("PLANE_ARTWORK_TRACE_CONTOUR_LIMIT");
  }
  if(original.empty())throw std::invalid_argument("PLANE_ARTWORK_TRACE_EMPTY");
  const bool fallback=!SameRings(original,candidate)&&!SafeSimplification(original,candidate,cancelled);
  const auto curves=CurveRecords(fallback?original:candidate);
  auto result=A::BuildGeometry(curves,false,cancelled);result["approximationBoundMm"]=simplify;
  result["traceDiagnostics"]=Map{{"contourCount",static_cast<unsigned long long>(curves.size())},{"filteredContours",static_cast<unsigned long long>(filtered)},
    {"pixelWidth",static_cast<unsigned long long>(pixels.Width)},{"pixelHeight",static_cast<unsigned long long>(pixels.Height)},{"threshold",threshold},{"invert",invert},{"topologyFallback",fallback}};return result;
}
class ArtworkService final : public G::IPlaneArtworkService {
  AUTO_REGIST_SERVICE(G::IPlaneArtworkService, ArtworkService)
public:
  void OnLoad() override {}void OnUnload() override {}
  G::PlaneArtworkBitmap NormalizeImage(const std::vector<std::uint8_t>& source) const override{return Normalize(source);}
  G::PlaneArtworkBitmap RasterizeText(const Map& options) const override{return TextBitmap(options);}
  Map Vectorize(const std::vector<std::uint8_t>& source,const Map& options,const std::function<bool()>& cancelled) const override{return Trace(source,options,cancelled);}
};
}
