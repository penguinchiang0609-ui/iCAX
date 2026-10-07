#pragma once

#include "Data/Variant.h"
#include <map>
#include <stdexcept>

namespace iCAX::TubeDesigner::NestingResultAccess
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;

    inline const ObjectMap& Object(const ObjectMap& parent, const char* field)
    {
        static const ObjectMap empty;
        const auto found = parent.find(field);
        return found != parent.end() && found->second.Is<ObjectMap>()
            ? std::get<ObjectMap>(found->second.m_Value) : empty;
    }

    inline const VariantArray& Array(const ObjectMap& parent, const char* field)
    {
        static const VariantArray empty;
        const auto found = parent.find(field);
        return found != parent.end() && found->second.Is<VariantArray>()
            ? std::get<VariantArray>(found->second.m_Value) : empty;
    }

    inline std::string Text(const ObjectMap& parent, const char* field)
    {
        const auto found = parent.find(field);
        return found != parent.end() && found->second.Is<std::string>()
            ? std::get<std::string>(found->second.m_Value) : std::string();
    }

    inline ObjectMap Summary(const ObjectMap& task)
    {
        const auto& result = Object(task, "result");
        ObjectMap summary{{ "ready", !result.empty() },
            { "revision", Text(task, "revision") },
            { "planCount", static_cast<unsigned long long>(Array(result, "plans").size()) }};
        for (const auto* field : { "status", "metrics", "proof", "diagnostics", "unplaced" })
            if (const auto found = result.find(field); found != result.end())
                summary.emplace(field, found->second);
        return summary;
    }

    inline ObjectMap TaskSummary(const ObjectMap& task)
    {
        if (task.empty()) return {};
        ObjectMap summary;
        for (const auto* field : { "parts", "revision" })
            if (const auto found = task.find(field); found != task.end())
                summary.emplace(field, found->second);
        ObjectMap request;
        for (const auto& [key, value] : Object(task, "request"))
            if (key != "lockedPlans") request.emplace(key, value);
        VariantArray lockedIDs;
        for (const auto& value : Array(Object(task, "request"), "lockedPlans"))
            if (value.Is<ObjectMap>())
                lockedIDs.emplace_back(ObjectMap{{ "id", Text(std::get<ObjectMap>(value.m_Value), "id") }});
        request["lockedPlans"] = std::move(lockedIDs);
        summary["request"] = std::move(request);
        summary["result"] = Summary(task);
        return summary;
    }

    // The list has one compact row per stock. Geometry and individual placements
    // remain in the authoritative scene until a stock is opened.
    inline ObjectMap Catalog(const ObjectMap& task, const std::string& revision)
    {
        if (revision.empty() || Text(task, "revision") != revision)
            throw std::invalid_argument("排样结果已变化，请重新读取");
        const auto& plans = Array(Object(task, "result"), "plans");
        std::map<std::string, unsigned long long> profiles, stocks;
        VariantArray profileKeys, stockIDs, rows;
        rows.reserve(plans.size());
        for (const auto& item : plans)
        {
            const auto& plan = std::get<ObjectMap>(item.m_Value);
            const auto profile = Text(plan, "profileKey"), stock = Text(plan, "stockTypeId");
            if (!profiles.contains(profile))
            {
                profiles.emplace(profile, profileKeys.size());
                profileKeys.emplace_back(profile);
            }
            if (!stocks.contains(stock))
            {
                stocks.emplace(stock, stockIDs.size());
                stockIDs.emplace_back(stock);
            }
            const auto number = [&](const char* field) -> Variant {
                const auto found = plan.find(field);
                return found == plan.end() ? Variant(0.0) : found->second;
            };
            rows.emplace_back(VariantArray{
                Text(plan, "id"), profiles.at(profile), stocks.at(stock),
                number("stockLength"), number("usedLength"), number("remainingLength"),
                number("partLength"), static_cast<unsigned long long>(Array(plan, "placements").size()),
                number("utilization")
            });
        }
        return {{ "revision", revision }, { "profileKeys", std::move(profileKeys) },
            { "stockTypeIds", std::move(stockIDs) }, { "rows", std::move(rows) }};
    }

    inline ObjectMap Plan(const ObjectMap& task, const std::string& revision, const std::string& id)
    {
        if (revision.empty() || Text(task, "revision") != revision)
            throw std::invalid_argument("排样结果已变化，请重新读取");
        for (const auto& item : Array(Object(task, "result"), "plans"))
        {
            const auto& plan = std::get<ObjectMap>(item.m_Value);
            if (Text(plan, "id") == id) return plan;
        }
        throw std::invalid_argument("所选排样结果不存在");
    }
}
