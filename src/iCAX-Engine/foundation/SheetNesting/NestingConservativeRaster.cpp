#include "SheetNestingRaster.h"
#include "NestingInternal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace iCAX::SheetNesting
{
namespace
{
    using Clock=std::chrono::steady_clock;
    constexpr std::size_t MaxGridSide=4096,MaxSheetCells=2097152,MaxSpecs=64,MaxInstances=5000,
        MaxVertices=512,MaxMaskCells=2097152,MaxAttempts=500000;
    struct GridPoint { long double X,Y; };
    struct Mask
    {
        std::size_t Width=0,Height=0,Words=0;
        double OriginX=0,OriginY=0,Area=0;
        std::vector<std::uint64_t> Rows;
    };

    // Exact grid-aligned vertices use open cell interiors, so a shared grid
    // edge need not be reserved twice. Other coordinates use an expanded
    // closed cell to conservatively retain uncertain boundary contacts.
    bool SegmentEntersCell(GridPoint a,GridPoint b,int x,int y,bool aligned,long double uncertainty)
    {
        long double lo=0,hi=1;
        const auto restrict=[&](long double start,long double delta,long double minimum,long double maximum)
        {
            if(!aligned){minimum-=uncertainty;maximum+=uncertainty;}
            if(delta==0)return aligned?(start>minimum&&start<maximum):(start>=minimum&&start<=maximum);
            long double u=(minimum-start)/delta,v=(maximum-start)/delta;
            if(u>v)std::swap(u,v);lo=std::max(lo,u);hi=std::min(hi,v);return aligned?lo<hi:lo<=hi;
        };
        return restrict(a.X,b.X-a.X,x,x+1)&&restrict(a.Y,b.Y-a.Y,y,y+1)&&(aligned?lo<hi:lo<=hi);
    }
    bool PointInside(const std::vector<GridPoint>& polygon,long double x,long double y)
    {
        bool inside=false;
        for(std::size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++)
        {
            const auto a=polygon[i],b=polygon[j];
            if((a.Y>y)!=(b.Y>y)&&x<(static_cast<long double>(b.X-a.X)*(y-a.Y)/(b.Y-a.Y)+a.X))inside=!inside;
        }
        return inside;
    }
    bool CellOccupied(const std::vector<GridPoint>& polygon,int x,int y,bool aligned,long double uncertainty)
    {
        for(std::size_t i=0;i<polygon.size();++i)
            if(SegmentEntersCell(polygon[i],polygon[(i+1)%polygon.size()],x,y,aligned,uncertainty))return true;
        return PointInside(polygon,static_cast<long double>(x)+.5L,static_cast<long double>(y)+.5L);
    }
    bool FitsMask(const Mask& m,const std::vector<std::uint64_t>& occupied,
                  std::size_t sheetWords,std::size_t x,std::size_t y)
    {
        const auto offset=x/64,shift=x%64;
        for(std::size_t row=0;row<m.Height;++row)for(std::size_t word=0;word<m.Words;++word)
        {
            const auto value=m.Rows[row*m.Words+word];if(!value)continue;
            const auto base=(y+row)*sheetWords+offset+word;
            if(base>=occupied.size()||(occupied[base]&(value<<shift)))return false;
            if(shift&&offset+word+1<sheetWords&&(occupied[base+1]&(value>>(64-shift))))return false;
        }
        return true;
    }
    void Occupy(const Mask& m,std::vector<std::uint64_t>& occupied,
                std::size_t sheetWords,std::size_t x,std::size_t y)
    {
        const auto offset=x/64,shift=x%64;
        for(std::size_t row=0;row<m.Height;++row)for(std::size_t word=0;word<m.Words;++word)
        {
            const auto value=m.Rows[row*m.Words+word];if(!value)continue;
            const auto base=(y+row)*sheetWords+offset+word;
            occupied[base]|=value<<shift;
            if(shift&&offset+word+1<sheetWords)occupied[base+1]|=value>>(64-shift);
        }
    }
    class ConservativeRaster final:public IStrategy
    {
    public:
        explicit ConservativeRaster(RasterStrategySettings settings):Settings(std::move(settings)){}
        Result Run(const Problem& problem,const Options& options) override
        {
            const auto started=Clock::now();Result result;std::vector<std::size_t> used(problem.Parts.size());
            bool cancelled=false;std::string reason="candidates-exhausted";
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
            const auto stop=[&]
            {
                if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;
                return cancelled||elapsed()>=options.TimeLimitSeconds||
                    result.Stats.PlacementAttempts>=std::min(options.MaxPlacementAttempts,MaxAttempts);
            };
            const auto finish=[&]
            {
                if(cancelled)reason="cancelled";
                else if(elapsed()>=options.TimeLimitSeconds)reason="time-limit";
                else if(result.Stats.PlacementAttempts>=std::min(options.MaxPlacementAttempts,MaxAttempts))reason="attempt-limit";
                for(std::size_t i=0;i<problem.Parts.size();++i)
                    if(used[i]<problem.Parts[i].Quantity)result.Remaining.push_back({problem.Parts[i].ID,problem.Parts[i].Quantity-used[i]});
                result.State=cancelled?Status::Cancelled:result.Remaining.empty()?Status::Complete:Status::Partial;
                if(result.State==Status::Complete&&reason=="candidates-exhausted")reason="complete";
                result.Stats.ElapsedSeconds=elapsed();
                result.Diagnostics.push_back("raster: "+reason+"; step="+std::to_string(Settings.Step)+" mm");
                return result;
            };
            if(Settings.Priority.size()!=problem.Parts.size())throw std::invalid_argument("Raster priority must contain every original part index exactly once.");
            std::vector<bool> seen(problem.Parts.size());
            for(auto i:Settings.Priority)
            {
                if(i>=seen.size()||seen[i])throw std::invalid_argument("Raster priority is not a permutation of original part indices.");
                seen[i]=true;
            }
            if(stop())return finish();
            if(problem.PartGap!=0||problem.Stocks.size()!=1||problem.Parts.size()>MaxSpecs)
            {reason="gap-stock-or-specification-scope";return finish();}
            const auto& stock=problem.Stocks.front();std::size_t total=0;
            if(stock.Quantity==0){reason="empty-inventory";return finish();}
            for(const auto& p:problem.Parts)
            {
                if(p.Quantity>MaxInstances-total){reason="instance-scope";return finish();}total+=p.Quantity;
                if(!p.Quantity)continue;
                if(p.Material!=stock.Material||p.Geometry.Radius>0||!p.Geometry.Holes.empty()||
                    p.Geometry.Outer.size()>MaxVertices||!Detail::AngleAllowed(p,0,problem.Tolerance))
                {reason="geometry-angle-or-material-scope";return finish();}
            }
            const double step=Settings.Step;
            const double wd=std::floor((stock.Width-2*problem.EdgeMargin)/step);
            const double hd=std::floor((stock.Height-2*problem.EdgeMargin)/step);
            if(!std::isfinite(wd)||!std::isfinite(hd)||wd<1||hd<1||wd>MaxGridSide||hd>MaxGridSide||wd*hd>MaxSheetCells)
            {reason="grid-scope";return finish();}
            const auto width=static_cast<std::size_t>(wd),height=static_cast<std::size_t>(hd),words=(width+63)/64;
            std::vector<Mask> masks(problem.Parts.size());std::size_t totalCells=0;
            for(std::size_t i=0;i<problem.Parts.size();++i)
            {
                if(stop())return finish();const auto& p=problem.Parts[i];if(!p.Quantity)continue;
                const auto box=Detail::Bounds(p.Geometry);auto& mask=masks[i];mask.OriginX=box.X;mask.OriginY=box.Y;
                const double cw=std::ceil(box.Width/step),ch=std::ceil(box.Height/step);
                if(!std::isfinite(cw)||!std::isfinite(ch)||cw<1||ch<1||cw>MaxGridSide||ch>MaxGridSide)
                {reason="part-grid-scope";return finish();}
                std::vector<GridPoint> polygon;polygon.reserve(p.Geometry.Outer.size());bool aligned=true;
                long double magnitude=std::max({1.,std::abs(box.X),std::abs(box.Y),box.Width,box.Height});
                for(const auto point:p.Geometry.Outer)
                {
                    if(stop())return finish();const long double gx=(static_cast<long double>(point.X)-box.X)/step;
                    const long double gy=(static_cast<long double>(point.Y)-box.Y)/step;
                    if(!std::isfinite(gx)||!std::isfinite(gy)){reason="part-grid-scope";return finish();}
                    const auto ix=std::round(gx),iy=std::round(gy);
                    if(gx!=ix||gy!=iy||box.X+static_cast<double>(ix)*step!=point.X||box.Y+static_cast<double>(iy)*step!=point.Y)aligned=false;
                    magnitude=std::max({magnitude,std::abs(static_cast<long double>(point.X)),std::abs(static_cast<long double>(point.Y))});
                    polygon.push_back({gx,gy});
                }
                mask.Width=static_cast<std::size_t>(cw);mask.Height=static_cast<std::size_t>(ch);
                // Reserve one extra boundary row/column for non-aligned input;
                // the closed-cell predicate below accounts for roundoff.
                if(!aligned){if(mask.Width==MaxGridSide||mask.Height==MaxGridSide){reason="part-grid-scope";return finish();}++mask.Width;++mask.Height;}
                if(mask.Width*mask.Height>MaxMaskCells-totalCells){reason="mask-scope";return finish();}
                totalCells+=mask.Width*mask.Height;mask.Words=(mask.Width+63)/64;
                const long double uncertainty=16*std::numeric_limits<double>::epsilon()*magnitude/step;
                if(!std::isfinite(uncertainty)||uncertainty>=1){reason="grid-numeric-scope";return finish();}
                if(aligned)for(auto& point:polygon){point.X=std::round(point.X);point.Y=std::round(point.Y);}
                mask.Rows.assign(mask.Words*mask.Height,0);
                for(std::size_t y=0;y<mask.Height;++y)for(std::size_t x=0;x<mask.Width;++x)
                {
                    if(stop())return finish();
                    if(CellOccupied(polygon,static_cast<int>(x),static_cast<int>(y),aligned,uncertainty))mask.Rows[y*mask.Words+x/64]|=std::uint64_t{1}<<(x%64);
                }
                mask.Area=Detail::Area(p.Geometry);++result.Stats.CandidatePatterns;
            }
            result.Stats.PreparationSeconds=elapsed();
            Layout layout{stock.ID,0,stock.Width,stock.Height,{},0};
            std::vector<std::uint64_t> occupied(words*height);
            bool progress=true;
            while(progress&&!stop())
            {
                progress=false;
                for(auto i:Settings.Priority)
                {
                    if(stop())break;if(used[i]>=problem.Parts[i].Quantity)continue;
                    const auto& p=problem.Parts[i];const auto& mask=masks[i];
                    if(mask.Width>width||mask.Height>height)continue;
                    bool found=false;const auto maxX=width-mask.Width,maxY=height-mask.Height;
                    const auto tryCell=[&](std::size_t x,std::size_t y)
                    {
                        if(stop())return false;++result.Stats.PlacementAttempts;
                        if(!FitsMask(mask,occupied,words,x,y))return false;
                        const Pose pose{problem.EdgeMargin+x*step-mask.OriginX,problem.EdgeMargin+y*step-mask.OriginY,0};
                        if(!Detail::InsideSheet(p.Geometry,pose,stock.Width,stock.Height,problem.EdgeMargin,problem.Tolerance))return false;
                        for(const auto& placed:layout.Placements)
                        {
                            if(stop())return false;
                            const auto other=std::find_if(problem.Parts.begin(),problem.Parts.end(),[&](const auto& q){return q.ID==placed.PartID;});
                            if(other==problem.Parts.end()||Detail::ShapesConflict(p.Geometry,pose,other->Geometry,placed.Transform,0,problem.Tolerance))return false;
                        }
                        Occupy(mask,occupied,words,x,y);
                        layout.Placements.push_back({p.ID,used[i]++,pose,0,false});layout.MaterialArea+=mask.Area;
                        return true;
                    };
                    if(Settings.Axis==RasterScanAxis::XThenY)
                        for(std::size_t x=0;x<=maxX&&!found&&!stop();++x)for(std::size_t y=0;y<=maxY&&!found&&!stop();++y)found=tryCell(x,y);
                    else
                        for(std::size_t y=0;y<=maxY&&!found&&!stop();++y)for(std::size_t x=0;x<=maxX&&!found&&!stop();++x)found=tryCell(x,y);
                    if(found){progress=true;if(!Settings.Interleave)break;}
                }
            }
            if(!layout.Placements.empty())result.Layouts.push_back(std::move(layout));
            result.Stats.PlacementSeconds=std::max(0.,elapsed()-result.Stats.PreparationSeconds);
            return finish();
        }
    private:RasterStrategySettings Settings;
    };
}
StrategyFactory MakeConservativeRasterFactory(RasterStrategySettings settings)
{
    if(!std::isfinite(settings.Step)||settings.Step<=0)throw std::invalid_argument("Raster step must be finite and positive.");
    if(settings.Axis!=RasterScanAxis::XThenY&&settings.Axis!=RasterScanAxis::YThenX)throw std::invalid_argument("Unknown raster scan axis.");
    return [settings=std::move(settings)]{return std::make_unique<ConservativeRaster>(settings);};
}
}
