#pragma once
#include "SheetNestingConstructionTrace.h"
#include <tuple>
#include <type_traits>

namespace iCAX::SheetNesting::Detail
{
template <class Build> inline void ObserveConstruction(const Options& options, ConstructionTraceStage stage, Build&& build) noexcept
{
    if (!options.ConstructionTrace) return;
    if(options.ConstructionObservation&&options.ConstructionObservation->Failed.load())
    {++options.ConstructionObservation->Omitted;return;}
    try
    {
        ConstructionTraceEvent event;
        event.Stage = stage;
        event.Route = options.ConstructionTraceRoute;
        event.Scope = options.ConstructionTraceScope;
        build(event);
        options.ConstructionTrace(event);
    }
    catch (...)
    {
        if(options.ConstructionObservation)
        {options.ConstructionObservation->Failed.store(true);++options.ConstructionObservation->Omitted;}
    }
}
template<class Value> inline void TraceRankValue(ConstructionTraceEvent& event,const std::string& name,const Value& value)
{
    if constexpr(std::is_arithmetic_v<Value>)event.Rank.emplace_back(name,static_cast<double>(value));
    else for(std::size_t i=0;i<value.size();++i)event.Rank.emplace_back(name+"["+std::to_string(i)+"]",static_cast<double>(value[i]));
}
template <class Tuple, std::size_t... I> inline void TraceTuple(ConstructionTraceEvent& event, const Tuple& key,
    const std::vector<std::string>& names, std::index_sequence<I...>)
{
    (TraceRankValue(event,I < names.size() ? names[I] : std::to_string(I),std::get<I>(key)), ...);
}
template <class Tuple> inline void TraceTuple(ConstructionTraceEvent& event, const Tuple& key, const std::vector<std::string>& names = {})
{
    TraceTuple(event, key, names, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}
}
