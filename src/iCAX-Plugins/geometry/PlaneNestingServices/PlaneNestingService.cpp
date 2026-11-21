#include <GeometryServices/PlaneNestingService.h>
#include <Services/ServicesHelper.h>
#include <iPlaneNesting.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace
{
    namespace PN = iCAX::GeometryServices::PlaneNesting;
    using Clock = std::chrono::steady_clock;
    constexpr double Tolerance = 1e-6;
    std::timed_mutex sdkMutex;

    struct Failure : std::runtime_error
    {
        int Code;
        Failure(int code, const std::string& message) : std::runtime_error(message), Code(code) {}
    };
    struct Cancelled {};
    void require(bool condition, const std::string& message, int code = PN2S_INVALID)
    {
        if (!condition) throw Failure(code, message);
    }
    void checkCode(int code)
    {
        if (code == PN2S_OK) return;
        const char* detail = PN2S_LastErrorDetail();
        throw Failure(code, detail && *detail ? detail : PN2_ErrorMessage(code));
    }
    template<class T> T checkHandle(T handle)
    {
        if (handle && !PN2_IsObjectError(handle)) return handle;
        const std::string detail = PN2S_LastErrorDetail();
        const int code = handle ? PN2_ErrorCodeFromObject(handle) : PN2S_INTERNAL;
        throw Failure(code, detail.empty() ? PN2_ErrorMessage(code) : detail);
    }
    struct Session
    {
        PN2_Session Handle = checkHandle(PN2_CreateSession());
        ~Session() { PN2_DeleteSession(Handle); }
        Session() = default;
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
    };
    double signedArea(const PN::Ring& ring)
    {
        double result = 0;
        const auto origin=ring.front();
        for (std::size_t i=0; i<ring.size(); ++i)
            result += (ring[i].X-origin.X)*(ring[(i+1)%ring.size()].Y-origin.Y)
                -(ring[(i+1)%ring.size()].X-origin.X)*(ring[i].Y-origin.Y);
        return result*.5;
    }
    double netArea(const PN::Ring& outer, const std::vector<PN::Ring>& holes)
    {
        double result = std::abs(signedArea(outer));
        for (const auto& hole : holes) result -= std::abs(signedArea(hole));
        return result;
    }
    PN::Ring normalizeRing(PN::Ring ring)
    {
        if (ring.size()>3 && ring.front().X==ring.back().X && ring.front().Y==ring.back().Y) ring.pop_back();
        require(ring.size()>=3, "A material ring requires at least three points");
        require(ring.size()<=8192, "A material ring exceeds 8192 points", PN2S_RESOURCE_LIMIT);
        for (auto point : ring) require(std::isfinite(point.X)&&std::isfinite(point.Y), "Nonfinite material coordinate");
        require(std::isfinite(signedArea(ring))&&std::abs(signedArea(ring))>1e-6, "Degenerate material ring");
        if (signedArea(ring)<0) std::reverse(ring.begin(),ring.end());
        return ring;
    }
    std::vector<PN2_Point> sdkRing(const PN::Ring& ring, PN::Point origin = {})
    {
        std::vector<PN2_Point> result;
        for (const auto point : ring) result.push_back({point.X-origin.X,point.Y-origin.Y});
        return result;
    }
    std::vector<PN2_Orientation> orientations(const PN::Part& part)
    {
        std::vector<PN2_Orientation> result;
        const auto add=[&](double lower,double upper)
        {
            require(std::isfinite(lower)&&std::isfinite(upper)&&lower<=upper&&upper-lower<=360., "Invalid angular interval");
            for (int flip=0;flip<=(part.AllowMirror?1:0);++flip)
                if (std::none_of(result.begin(),result.end(),[&](const auto& pose){return pose.horizontal_flip==flip&&pose.min_angle==lower&&pose.max_angle==upper;}))
                    result.push_back({flip,lower,upper});
        };
        for (auto angle : part.AnglesDeg) add(angle,angle);
        for (auto interval : part.RotationIntervals) add(interval.MinDeg,interval.MaxDeg);
        if (result.empty()) add(0,0);
        require(result.size()<=64, "At most 64 orientation constraints including reflected poses",PN2S_RESOURCE_LIMIT);
        return result;
    }
    struct Concrete { std::size_t PartIndex,Ordinal; };

    class Service final : public iCAX::GeometryServices::IPlaneNestingService
    {
        AUTO_REGIST_SERVICE(iCAX::GeometryServices::IPlaneNestingService,Service)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        PN::Result Solve(const PN::Request& source,const std::function<bool()>& cancelled) const override
        {
            PN::Result result;result.InputKey=source.InputKey;
            const auto started=Clock::now();
            const auto remaining=[&] {return source.TimeLimitSeconds-std::chrono::duration<double>(Clock::now()-started).count();};
            const auto checkpoint=[&]
            {
                if (cancelled&&cancelled()) throw Cancelled{};
                require(remaining()>0,"Adapter time budget exhausted",PN2S_TIMEOUT);
            };
            PN::Request request;
            try
            {
                request=source;
                require(std::isfinite(request.TimeLimitSeconds)&&request.TimeLimitSeconds>0&&request.TimeLimitSeconds<=3600,"TimeLimitSeconds must be in (0,3600]");
                require(std::isfinite(request.PartGap)&&request.PartGap>=0&&request.PartGap<=1000,"PartGap must be in [0,1000]");
                require(std::isfinite(request.StockMargin)&&request.StockMargin>=0&&request.StockMargin<=1e6,"Invalid stock margin");
                require(request.Stocks.size()<=100,"At most 100 physical stock boards",PN2S_RESOURCE_LIMIT);
                std::set<std::string> ids;
                std::vector<std::vector<PN2_Orientation>> poses;
                for (auto& part : request.Parts)
                {
                    checkpoint();
                    require(!part.Id.empty()&&ids.insert(part.Id).second,"Part IDs must be nonempty and unique");
                    require(part.Quantity>=0,"Negative part quantity");
                    require(static_cast<std::size_t>(part.Quantity)<=500-result.Statistics.RequiredCount,"At most 500 concrete parts",PN2S_RESOURCE_LIMIT);
                    result.Statistics.RequiredCount+=static_cast<std::size_t>(part.Quantity);
                    require(std::isfinite(part.Pivot.X)&&std::isfinite(part.Pivot.Y),"Nonfinite part pivot");
                    part.Outer=normalizeRing(std::move(part.Outer));
                    require(part.Holes.size()<=512,"At most 512 holes per part",PN2S_RESOURCE_LIMIT);
                    for (auto& hole : part.Holes) hole=normalizeRing(std::move(hole));
                    poses.push_back(orientations(part));
                }
                ids.clear();
                for (auto& stock : request.Stocks)
                {
                    checkpoint();
                    require(!stock.Id.empty()&&ids.insert(stock.Id).second,"Physical stock IDs must be nonempty and unique");
                    stock.Outline=normalizeRing(std::move(stock.Outline));
                    require(stock.Holes.size()<=512,"At most 512 holes per stock",PN2S_RESOURCE_LIMIT);
                    for (auto& hole : stock.Holes) hole=normalizeRing(std::move(hole));
                }
                std::unique_lock<std::timed_mutex> lock(sdkMutex,std::defer_lock);
                while (!lock.try_lock_for(std::chrono::milliseconds(5))) checkpoint();
                checkpoint();
                Session session;
                result.ProviderVersion=PN2S_Version();
                checkCode(PN2S_SetMaterialGap(session.Handle,request.PartGap));
                checkCode(PN2S_SetRandomSeed(session.Handle,request.RandomSeed));
                std::vector<PN2_Part> parts;
                std::unordered_map<PN2_Part,Concrete> concrete;
                for (std::size_t index=0;index<request.Parts.size();++index)
                {
                    checkpoint();const auto& part=request.Parts[index];
                    const auto outer=sdkRing(part.Outer);
                    const auto shape=checkHandle(PN2_AddShape(session.Handle,outer.data(),static_cast<int>(outer.size()),{part.Pivot.X,part.Pivot.Y}));
                    for (const auto& ring : part.Holes)
                    {
                        const auto hole=sdkRing(ring);
                        checkCode(PN2_ShapeAddHole(session.Handle,shape,hole.data(),static_cast<int>(hole.size())));
                    }
                    if (!part.Quantity) continue;
                    std::vector<PN2_Part> added(static_cast<std::size_t>(part.Quantity));
                    checkCode(PN2S_AddMaterialParts(session.Handle,shape,poses[index].data(),static_cast<int>(poses[index].size()),part.Quantity,added.data()));
                    for (std::size_t ordinal=0;ordinal<added.size();++ordinal)
                    {
                        checkHandle(added[ordinal]);concrete.emplace(added[ordinal],Concrete{index,ordinal});parts.push_back(added[ordinal]);
                    }
                }
                std::vector<PN2_Sheet> sheets;
                std::vector<int> quantities(request.Stocks.size(),1);
                std::unordered_map<PN2_Sheet,std::size_t> stockIndices;
                std::vector<PN::Point> origins;
                for (std::size_t index=0;index<request.Stocks.size();++index)
                {
                    checkpoint();const auto& stock=request.Stocks[index];
                    PN::Point origin{stock.Outline.front().X,stock.Outline.front().Y};
                    for (auto p : stock.Outline) {origin.X=std::min(origin.X,p.X);origin.Y=std::min(origin.Y,p.Y);}
                    const auto outline=sdkRing(stock.Outline,origin);
                    const auto sheet=checkHandle(PN2_AddPolygonalSheetWithBorderGap(session.Handle,outline.data(),static_cast<int>(outline.size()),request.StockMargin));
                    for (const auto& ring : stock.Holes)
                    {
                        const auto hole=sdkRing(ring,origin);
                        checkCode(PN2_SheetAddDefectWithOffset(session.Handle,sheet,hole.data(),static_cast<int>(hole.size()),request.StockMargin));
                    }
                    sheets.push_back(sheet);stockIndices.emplace(sheet,index);origins.push_back(origin);
                }
                result.Checks.InputAccepted=true;
                if (parts.empty()||sheets.empty())
                {
                    result.State=parts.empty()?PN::Status::Completed:PN::Status::Partial;
                    result.StopReason=parts.empty()?"Completed":"NoStock";
                    for (const auto& [handle,item] : concrete) result.UnplacedParts.push_back({request.Parts[item.PartIndex].Id,item.Ordinal,"NoStock"});
                    result.Checks.GeometryValid=result.Checks.DemandConserved=result.Checks.StockConserved=result.Checks.OrientationsValid=result.Valid=true;
                }
                else
                {
                    checkpoint();
                    const auto multi=checkHandle(PN2_MultiNest(session.Handle,sheets.data(),static_cast<int>(sheets.size()),quantities.data(),parts.data(),static_cast<int>(parts.size()),remaining()));
                    if (cancelled&&cancelled()) throw Cancelled{};
                    PN2S_MaterialResultInfo info{};info.struct_size=sizeof(info);
                    checkCode(PN2S_GetMaterialResultInfos(session.Handle,multi,&info));
                    require(info.is_material!=0,"Expected polygon material result",PN2S_INTERNAL);
                    result.Checks.Diagnostics.push_back("Polygon boundaries, holes and clearances validated by iPlaneNesting.MaterialHost");
                    result.Statistics.Trials=info.trials;
                    result.Diagnostic=info.warning_utf8?info.warning_utf8:"";
                    int boardCount=0;checkCode(PN2_GetMultiResultInfos(session.Handle,multi,&boardCount));
                    std::set<PN2_Part> placed;
                    std::set<PN2_Sheet> used;
                    for (int boardIndex=0;boardIndex<boardCount;++boardIndex)
                    {
                        PN2_Sheet sheet=nullptr;
                        const auto board=checkHandle(PN2_GetResult(session.Handle,multi,boardIndex,&sheet));
                        require(stockIndices.contains(sheet)&&used.insert(sheet).second,"Unknown or reused physical stock board",PN2S_INTERNAL);
                        const auto stockIndex=stockIndices.at(sheet);const auto& stock=request.Stocks[stockIndex];
                        double occupied=0;int count=0;checkCode(PN2_GetResultInfos(session.Handle,board,&occupied,&count));
                        PN::BoardUsage usage;usage.PhysicalStockId=stock.Id;usage.StockNetArea=netArea(stock.Outline,stock.Holes);
                        for (int k=0;k<count;++k)
                        {
                            PN2_Part handle=nullptr;PN2_Point position{};PN2_Orientation orientation{};
                            checkCode(PN2_GetKthNestedPart(session.Handle,board,k,&handle,&position,&orientation));
                            require(concrete.contains(handle)&&placed.insert(handle).second,"Unknown or repeated concrete part",PN2S_INTERNAL);
                            const auto identity=concrete.at(handle);const auto& part=request.Parts[identity.PartIndex];
                            require(std::isfinite(position.x)&&std::isfinite(position.y)&&std::isfinite(orientation.min_angle),"Nonfinite output pose",PN2S_INTERNAL);
                            require(std::any_of(poses[identity.PartIndex].begin(),poses[identity.PartIndex].end(),[&](const auto& pose){return pose.horizontal_flip==orientation.horizontal_flip&&orientation.min_angle>=pose.min_angle-1e-8&&orientation.min_angle<=pose.max_angle+1e-8;}),"Output violates source orientation constraints",PN2S_INTERNAL);
                            PN::Placement placement{stock.Id,part.Id,identity.Ordinal,position.x+origins[stockIndex].X,position.y+origins[stockIndex].Y,orientation.min_angle,orientation.horizontal_flip!=0};
                            result.Placements.push_back(std::move(placement));
                            ++usage.PlacedCount;usage.PlacedMaterialArea+=netArea(part.Outer,part.Holes);
                        }
                        usage.Utilization=usage.StockNetArea>0?usage.PlacedMaterialArea/usage.StockNetArea:0;
                        result.Statistics.StockNetArea+=usage.StockNetArea;result.Statistics.PlacedMaterialArea+=usage.PlacedMaterialArea;
                        result.Boards.push_back(std::move(usage));
                    }
                    int missing=0;checkCode(PN2S_GetMultiUnplacedCount(session.Handle,multi,&missing));
                    std::set<PN2_Part> unplaced;
                    for (int k=0;k<missing;++k)
                    {
                        PN2_Part handle=nullptr;checkCode(PN2S_GetMultiUnplacedPart(session.Handle,multi,k,&handle));
                        require(concrete.contains(handle)&&!placed.contains(handle)&&unplaced.insert(handle).second,"Invalid unplaced concrete identity",PN2S_INTERNAL);
                        const auto identity=concrete.at(handle);
                        result.UnplacedParts.push_back({request.Parts[identity.PartIndex].Id,identity.Ordinal,info.stopped?"SearchStopped":"NotPlaced"});
                    }
                    require(placed.size()+unplaced.size()==parts.size(),"Concrete part demand was not conserved",PN2S_INTERNAL);
                    result.Checks.GeometryValid=result.Checks.DemandConserved=result.Checks.StockConserved=result.Checks.OrientationsValid=result.Valid=true;
                    result.State=result.UnplacedParts.empty()?PN::Status::Completed:PN::Status::Partial;
                    result.StopReason=info.stopped?"SearchStopped":result.UnplacedParts.empty()?"Completed":"UnplacedDemand";
                }
                if (cancelled&&cancelled()) throw Cancelled{};
            }
            catch (const Cancelled&)
            {
                result.Valid=false;
                result.State=PN::Status::Cancelled;result.StopReason="Cancelled";result.Diagnostic="Cancellation observed; SDK output discarded";
            }
            catch (const Failure& error)
            {
                result.Valid=false;
                result.Diagnostic=error.what();
                switch(error.Code)
                {
                case PN2S_INVALID: result.State=PN::Status::InvalidInput;result.StopReason="InvalidInput";break;
                case PN2S_UNSUPPORTED: result.State=PN::Status::Unsupported;result.StopReason="Unsupported";break;
                case PN2S_RESOURCE_LIMIT: result.State=PN::Status::ResourceLimit;result.StopReason="ResourceLimit";break;
                case PN2S_TIMEOUT: result.State=PN::Status::BudgetExhausted;result.StopReason="BudgetExhausted";break;
                default: result.State=PN::Status::Failed;result.StopReason="EngineError";break;
                }
            }
            catch (const std::exception& error)
            {
                result.Valid=false;
                result.State=PN::Status::Failed;result.StopReason="AdapterError";result.Diagnostic=error.what();
            }
            catch (...)
            {
                result.Valid=false;result.State=PN::Status::Failed;result.StopReason="AdapterError";result.Diagnostic="Unknown adapter exception";
            }
            if (!result.Valid)
            {
                result.Placements.clear();result.Boards.clear();result.UnplacedParts.clear();
                result.Statistics.PlacedMaterialArea=result.Statistics.StockNetArea=0;
                result.Checks.GeometryValid=result.Checks.DemandConserved=result.Checks.StockConserved=result.Checks.OrientationsValid=false;
                result.Checks.Diagnostics.push_back(result.Diagnostic);
                if (result.Checks.InputAccepted&&result.Statistics.RequiredCount<=500)
                    for (const auto& part : request.Parts) if (part.Quantity>=0&&part.Quantity<=500)
                        for (int k=0;k<part.Quantity;++k) result.UnplacedParts.push_back({part.Id,static_cast<std::size_t>(k),result.StopReason});
            }
            result.Statistics.PlacedCount=result.Placements.size();result.Statistics.UnplacedCount=result.UnplacedParts.size();result.Statistics.UsedStockCount=result.Boards.size();
            result.Statistics.Utilization=result.Statistics.StockNetArea>0?result.Statistics.PlacedMaterialArea/result.Statistics.StockNetArea:0;
            result.Statistics.ElapsedMs=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
            return result;
        }
    };
}

extern "C" __declspec(dllexport) iCAX::GeometryServices::IPlaneNestingService* CreatePlaneNestingService() { return new Service; }
extern "C" __declspec(dllexport) void DestroyPlaneNestingService(iCAX::GeometryServices::IPlaneNestingService* service) { delete service; }
