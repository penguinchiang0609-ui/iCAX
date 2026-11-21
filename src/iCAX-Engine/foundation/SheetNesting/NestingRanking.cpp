#include "NestingExecution.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
    namespace
    {
        // MSVC long double has only binary64 precision. Preserve the low terms
        // when a thin material frame subtracts almost equal polygon moments.
        struct DoubleDouble
        {
            double Hi=0,Lo=0;
            DoubleDouble()=default;
            DoubleDouble(double value):Hi(value){}
            DoubleDouble(double hi,double lo):Hi(hi),Lo(lo){}
            bool Positive()const{return Hi>0||(Hi==0&&Lo>0);}
            bool Negative()const{return Hi<0||(Hi==0&&Lo<0);}
            double Value()const{return Hi+Lo;}
        };
        DoubleDouble TwoSum(double a,double b)
        {
            const double sum=a+b,tail=sum-a;
            return {sum,(a-(sum-tail))+(b-tail)};
        }
        DoubleDouble Product(double a,double b)
        {const double product=a*b;return {product,std::fma(a,b,-product)};}
        DoubleDouble operator+(DoubleDouble a,DoubleDouble b)
        {
            auto sum=TwoSum(a.Hi,b.Hi);const auto tail=TwoSum(a.Lo,b.Lo);
            sum=TwoSum(sum.Hi,sum.Lo+tail.Hi);
            return TwoSum(sum.Hi,sum.Lo+tail.Lo);
        }
        DoubleDouble operator-(DoubleDouble value){return {-value.Hi,-value.Lo};}
        DoubleDouble operator-(DoubleDouble a,DoubleDouble b){return a+-b;}
        DoubleDouble operator*(DoubleDouble a,DoubleDouble b)
        {
            return Product(a.Hi,b.Hi)+Product(a.Hi,b.Lo)+
                Product(a.Lo,b.Hi)+Product(a.Lo,b.Lo);
        }
        DoubleDouble operator/(DoubleDouble a,DoubleDouble b)
        {
            const double first=a.Hi/b.Hi;auto remainder=a-b*first;
            const double second=remainder.Hi/b.Hi;remainder=remainder-b*second;
            return DoubleDouble(first)+DoubleDouble(second)+DoubleDouble(remainder.Hi/b.Hi);
        }
        struct MaterialMeasure { DoubleDouble Area; Point Centroid; };
        struct RingMoments { DoubleDouble TwiceArea,X,Y; };

        RingMoments MeasureRing(const std::vector<Point>& ring,Point origin)
        {
            DoubleDouble twiceArea,x,y;
            for(std::size_t i=0;i<ring.size();++i)
            {
                const auto a=ring[i],b=ring[(i+1)%ring.size()];
                const auto ax=DoubleDouble(a.X)-origin.X,ay=DoubleDouble(a.Y)-origin.Y;
                const auto bx=DoubleDouble(b.X)-origin.X,by=DoubleDouble(b.Y)-origin.Y;
                const auto cross=ax*by-ay*bx;
                twiceArea=twiceArea+cross;x=x+(ax+bx)*cross;y=y+(ay+by)*cross;
            }
            return twiceArea.Negative()?RingMoments{-twiceArea,-x,-y}:RingMoments{twiceArea,x,y};
        }

        MaterialMeasure MeasureMaterial(const Shape& shape)
        {
            if(shape.Radius>0)return {Detail::Area(shape),shape.Center};
            // A common local origin avoids subtracting large absolute products.
            // Ring winding is immaterial; every hole subtracts actual material.
            const auto origin=shape.Outer.front();auto material=MeasureRing(shape.Outer,origin);
            for(const auto& ring:shape.Holes)
            {
                const auto hole=MeasureRing(ring,origin);
                material.TwiceArea=material.TwiceArea-hole.TwiceArea;
                material.X=material.X-hole.X;material.Y=material.Y-hole.Y;
            }
            if(!material.TwiceArea.Positive())throw std::logic_error("Invalid material area in assessed nesting quality.");
            const auto denominator=material.TwiceArea*3;
            return {material.TwiceArea/2,{(DoubleDouble(origin.X)+material.X/denominator).Value(),
                (DoubleDouble(origin.Y)+material.Y/denominator).Value()}};
        }
    }

    bool Eligible(const JobResult& job)
    { return job.Checked.Valid && (job.Output.State == Status::Complete || job.Output.State == Status::Partial || job.Output.State == Status::Cancelled); }
    QualityKey Quality(const JobResult& job, const Problem& input, RankingPolicy policy)
    {
        std::size_t missing = 0;
        std::unordered_map<std::string,const Part*> parts;
        std::unordered_map<std::string,MaterialMeasure> measures;
        for(const auto& part:input.Parts){parts.emplace(part.ID,&part);measures.emplace(part.ID,MeasureMaterial(part.Geometry));}
        for (const auto& item : job.Output.Remaining) missing += item.Quantity;
        double topSum = 0, rightSum = 0, extent = 1;
        for (const auto& stock : input.Stocks) extent = std::max(extent, stock.Width + stock.Height);
        struct Board { std::string Material; double MaterialArea=0, FreeArea=0, SheetArea=0, EnvelopeArea=0; };
        std::unordered_map<std::string,const Stock*> stocks;
        for(const auto& stock:input.Stocks)stocks.emplace(stock.ID,&stock);
        std::vector<Board> boards;
        std::map<std::string,std::size_t> tails;
        std::unordered_map<std::string,std::size_t> placedCounts,stockCounts;
        std::vector<double> occupiedAreas,usedSectionAreas;
        double placedArea=0;
        DoubleDouble centroidArea,centroidX,centroidY;
        for (const auto& layout : job.Output.Layouts)
        {
            double top = 0, right = 0, materialArea=0;
            double minX=std::numeric_limits<double>::infinity(),minY=minX;
            double maxX=-minX,maxY=-minX;
            for (const auto& placement : layout.Placements)
            {
                const auto part = parts.find(placement.PartID);
                if (part == parts.end()) continue;
                ++placedCounts[placement.PartID];
                const auto box = Detail::Bounds(part->second->Geometry, placement.Transform);
                top = std::max(top, box.Y + box.Height); right = std::max(right, box.X + box.Width);
                minX=std::min(minX,box.X);minY=std::min(minY,box.Y);
                maxX=std::max(maxX,box.X+box.Width);maxY=std::max(maxY,box.Y+box.Height);
                materialArea+=Detail::Area(part->second->Geometry);
                const auto& measure=measures.at(placement.PartID);
                const auto center=Detail::Transform(measure.Centroid,placement.Transform);
                centroidArea=centroidArea+measure.Area;
                centroidX=centroidX+measure.Area*center.X;centroidY=centroidY+measure.Area*center.Y;
            }
            topSum += top; rightSum += right;
            const auto stock=stocks.find(layout.StockID);
            const auto material=stock==stocks.end()?std::string{}:stock->second->Material;
            const double sheetArea=layout.Width*layout.Height;
            const auto index=boards.size();
            const double envelopeArea=layout.Placements.empty()?0.:(maxX-minX)*(maxY-minY);
            ++stockCounts[layout.StockID];occupiedAreas.push_back(envelopeArea);
            // A narrow column still consumes the board's entire fixed width
            // over its used length. Do not erase lateral or leading blanks
            // from the utilization denominator by cropping to part bounds.
            usedSectionAreas.push_back(layout.Placements.empty()?0.:layout.Width*
                std::clamp(top+input.EdgeMargin,0.,layout.Height));
            boards.push_back({material,materialArea,std::max(0.,sheetArea-materialArea),sheetArea,envelopeArea});
            placedArea+=materialArea;
            const auto tail=tails.find(material);
            if(tail==tails.end()||boards[index].FreeArea>boards[tail->second].FreeArea)tails[material]=index;
        }
        const double linearQuantum = input.Tolerance;
        const double areaQuantum = input.Tolerance * extent;
        const auto linear = [&](double value) { return std::round(value / linearQuantum) * linearQuantum; };
        const auto areaValue = [&](double value) { return std::round(value / areaQuantum) * areaQuantum; };
        const double area = std::round(job.Checked.UsedEnvelopeArea / areaQuantum) * areaQuantum;
        // Assessment already selects and maps the displayed logical tails,
        // including tolerance ties. Score those same certified sheets.
        if(job.Remnants.Measured)for(const auto i:job.Remnants.FinalLayoutIndices)
            if(i<boards.size())tails[boards[i].Material]=i;
        // These are physical material areas, not enclosing-box areas and not
        // sums of scattered gaps labelled as a reusable rectangle.
        double tailFreeArea=0,tailMaterialArea=0,tailEnvelopeArea=0;
        std::vector<double> frontUtilization;
        for(const auto& [material,tail]:tails)
        {
            tailFreeArea+=boards[tail].FreeArea;
            tailMaterialArea+=boards[tail].MaterialArea;
            tailEnvelopeArea+=boards[tail].EnvelopeArea;
            std::vector<double> prefix;
            for(std::size_t i=0;i<boards.size();++i)if(boards[i].Material==material&&i!=tail)
                prefix.push_back(-areaValue(boards[i].MaterialArea)/boards[i].SheetArea);
            frontUtilization.insert(frontUtilization.end(),prefix.begin(),prefix.end());
        }
        // Material names are identities, not priorities. Renaming compatible
        // groups must not reverse otherwise identical portfolio comparisons.
        std::sort(frontUtilization.begin(),frontUtilization.end());
        // Partial layouts must gain material, not receive a reward for leaving
        // more requested material unplaced on an apparently empty last sheet.
        const double consolidation=missing==0?-areaValue(tailFreeArea):-areaValue(placedArea);
        // The explicitly requested older policies retain these consolidation
        // terms. Utilization below leaves every remnant/count reward neutral;
        // final position ties are considered only after both ratios tie.
        const double meanY=linear(centroidArea.Positive()?(centroidY/centroidArea).Value():0.);
        const double meanX=linear(centroidArea.Positive()?(centroidX/centroidArea).Value():0.);
        if(policy==RankingPolicy::Utilization)
        {
            // Inventory identities, not placement order, determine net area.
            // This avoids summation-order noise deciding between complete
            // layouts before their actual utilization is compared.
            DoubleDouble netMaterial,consumedStock,envelopes,sections;
            for(const auto& part:input.Parts)
                netMaterial=netMaterial+measures.at(part.ID).Area*static_cast<double>(placedCounts[part.ID]);
            for(const auto& stock:input.Stocks)
                consumedStock=consumedStock+Product(stock.Width,stock.Height)*static_cast<double>(stockCounts[stock.ID]);
            std::sort(occupiedAreas.begin(),occupiedAreas.end());
            for(const auto value:occupiedAreas)envelopes=envelopes+value;
            std::sort(usedSectionAreas.begin(),usedSectionAreas.end());
            for(const auto value:usedSectionAreas)sections=sections+value;
            const double material=netMaterial.Value(),stockArea=consumedStock.Value(),occupied=envelopes.Value();
            const double usedSection=sections.Value();
            const auto utilization=[](double actual,double consumed)
                {return consumed>0?std::clamp(actual/consumed,0.,1.):0.;};
            const std::size_t incomplete=job.Checked.Complete?0:1;
            // An omitted large original cannot buy an apparently dense winner.
            // Partial layouts are progress only and never compete as complete
            // layouts; among them retain more real requested material.
            return std::tuple(incomplete,std::size_t{0},incomplete?-material:0.,false,
                -utilization(material,stockArea),-utilization(material,usedSection),std::vector<double>{},
                stockArea,usedSection,occupied,linear(topSum),linear(rightSum),meanY,meanX);
        }
        if (policy == RankingPolicy::RegularRemnant)
        {
            // Evaluate one already selected logical tail per material group;
            // never invent a box spanning incompatible sheets or materials.
            // Sum their net original material and their individual AABB areas.
            // This area-weighted aggregate is not the mean of per-sheet ratios
            // and does not promise improvement on every material group alone.
            // Only the already selected logical tails contribute. Do not sum
            // preceding sheets' rectangles or change the physical tail choice.
            const double tailMaterial=areaValue(tailMaterialArea);
            const double tailEnvelope=areaValue(tailEnvelopeArea);
            const double tailDensity=tailEnvelope>0
                ? -std::clamp(tailMaterial/tailEnvelope,0.,1.) : 0.;
            bool available=job.Remnants.Measured&&std::isfinite(job.Remnants.LastSheetRegularArea)&&
                job.Remnants.LastSheetRegularArea>=0;
            double remnant=available?-areaValue(job.Remnants.LastSheetRegularArea):0.;
            if(!std::isfinite(remnant)){available=false;remnant=0.;}
            // A partial may not win by omitting a larger original and gaining
            // a bigger empty rectangle. Unknown measurement is explicit and
            // finite; even a certified zero beats an unavailable certificate.
            const double partialMaterial=missing?-areaValue(placedArea):0.;
            return std::tuple(missing,job.Output.Layouts.size(),partialMaterial,!available,remnant,
                consolidation,frontUtilization,tailMaterial,tailDensity,linear(topSum),linear(rightSum),0.,meanY,meanX);
        }
        return policy == RankingPolicy::LowThenLeft
            ? std::tuple(missing,job.Output.Layouts.size(),0.,false,0.,consolidation,frontUtilization,0.,0.,linear(topSum),linear(rightSum),area,meanY,meanX)
            : std::tuple(missing,job.Output.Layouts.size(),0.,false,0.,consolidation,frontUtilization,0.,0.,area,linear(topSum),linear(rightSum),meanY,meanX);
    }
}
