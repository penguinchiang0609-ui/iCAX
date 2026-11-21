struct OpenWallBranch
{
    std::size_t Shoulder;
    Wall Region;
};

bool ProvesOpenBranchRoot(const Model& model, const std::vector<GuideSpans>& traces,
    const Edge& before, const Edge& after, const Edge& cap)
{
    const Point inward = Inward(before, 0);
    const Point start = before.Shape->End;
    const Point end = after.Shape->Start;
    for (std::size_t i = 0; i < model.OuterCount; ++i) {
        const Edge& opposing = model.Edges[i];
        if (opposing.Shape->IsArc || Dot(Inward(opposing, 0), inward) > -1 + AngularTolerance ||
            Dot(opposing.Shape->Start - start, inward) <= model.Tolerance)
            continue;
        const Point axis = Tangent(*opposing.Shape, 0);
        const double length = CurveLength(*opposing.Shape);
        const double first = Dot(end - opposing.Shape->Start, axis) / length;
        const double last = Dot(start - opposing.Shape->Start, axis) / length;
        const double slack = 3 * model.Tolerance / length;
        if (first <= slack || last >= 1 - slack || last - first <= slack ||
            !SweepsIntervalTo(model, traces[i], cap, first, last))
            continue;
        const auto supportsShoulder = [&](const Edge& edge, double event, bool startsAtEvent) {
            return std::ranges::any_of(traces[i], [&](const SweepSpan& span) {
                return span.Target == &edge && (startsAtEvent
                    ? std::abs(span.First - event) <= slack && span.Last > event + slack
                    : std::abs(span.Last - event) <= slack && span.First < event - slack);
            });
        };
        // The same opposing skin hits each shoulder and sweeps farther to
        // the cap between them. This locates the root on the shoulder line.
        if (supportsShoulder(after, first, false) && supportsShoulder(before, last, true))
            return true;
    }
    return false;
}

std::optional<OpenWallBranch> FindOpenWallBranch(const Model& model, const std::vector<GuideSpans>& traces)
{
    if (model.OuterCount < 7)
        return std::nullopt;
    for (std::size_t i = 0; i < model.OuterCount; ++i) {
        const Edge& before = model.Edges[i];
        const Edge& firstSide = AlongOuter(model, before, 1);
        const Edge& cap = AlongOuter(model, before, 2);
        const Edge& secondSide = AlongOuter(model, before, 3);
        const Edge& after = AlongOuter(model, before, 4);
        if (before.Shape->IsArc || firstSide.Shape->IsArc || cap.Shape->IsArc ||
            secondSide.Shape->IsArc || after.Shape->IsArc || !Collinear(model, before, after))
            continue;
        const Point along = Tangent(*before.Shape, 0);
        const Point inward = Inward(before, 0);
        const Point stem = firstSide.Shape->End - firstSide.Shape->Start;
        const Point root = after.Shape->Start - before.Shape->End;
        if (Dot(along, Tangent(*after.Shape, 0)) <= 0 || Dot(root, along) <= model.Tolerance ||
            Dot(stem, inward) >= -model.Tolerance || std::abs(Dot(stem, along)) > model.Tolerance ||
            Dot(Tangent(*cap.Shape, 0), along) < 1 - AngularTolerance ||
            Dot(Inward(firstSide, 0), Inward(secondSide, 0)) > -1 + AngularTolerance ||
            Distance(cap.Shape->End - cap.Shape->Start, root) > model.Tolerance)
            continue;
        if (!SweepsOverOpposingSide(model, firstSide, secondSide, traces[&firstSide - model.Edges.data()]) ||
            !SweepsOverOpposingSide(model, secondSide, firstSide, traces[&secondSide - model.Edges.data()]) ||
            !ProvesOpenBranchRoot(model, traces, before, after, cap))
            continue;
        const std::vector<int> sides{firstSide.Shape->Id, secondSide.Shape->Id};
        return OpenWallBranch{i, {"分支壁", sides, {sides[0], cap.Shape->Id, sides[1]},
            {before.Shape->End, firstSide.Shape->End, cap.Shape->End, after.Shape->Start}}};
    }
    return std::nullopt;
}

struct OpenRailPair
{
    const Edge* Guide;
    const Edge* Opposing;
};

std::optional<OpenChainRail> MakePairedOpenRail(const Model& model, const OpenRailPair& pair,
    bool startsAtCap, bool endsAtCap)
{
    const Curve& guide = *pair.Guide->Shape;
    const Curve& opposing = *pair.Opposing->Shape;
    const Point along = Tangent(guide, 0);
    const Point inward = Inward(*pair.Guide, 0);
    const double first = std::max(0.0, Dot(opposing.End - guide.Start, along));
    const double last = std::min(CurveLength(guide), Dot(opposing.Start - guide.Start, along));
    if (last - first <= 3 * model.Tolerance)
        return std::nullopt;
    const Point outerFirst = guide.Start + along * first;
    const Point outerLast = guide.Start + along * last;
    const auto innerFirst = IntersectSupport(outerFirst, inward, *pair.Opposing);
    const auto innerLast = IntersectSupport(outerLast, inward, *pair.Opposing);
    if (!IsOnSupport(model, *pair.Opposing, innerFirst) || !IsOnSupport(model, *pair.Opposing, innerLast))
        return std::nullopt;
    return OpenChainRail{pair.Guide, {pair.Opposing},
        startsAtCap ? guide.Start : outerFirst, endsAtCap ? guide.End : outerLast,
        startsAtCap ? opposing.End : innerFirst->PointOnSupport,
        endsAtCap ? opposing.Start : innerLast->PointOnSupport};
}

std::optional<Plan> OpenPairedChainPlan(const Model& model, const std::vector<OpenRailPair>& pairs)
{
    std::vector<OpenChainRail> rails;
    Plan plan;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const auto rail = MakePairedOpenRail(model, pairs[i], i == 0, i + 1 == pairs.size());
        if (!rail)
            return std::nullopt;
        const int guideId = rail->Guide->Shape->Id, opposingId = pairs[i].Opposing->Shape->Id;
        plan.Walls.push_back({"直壁 " + std::to_string(i + 1), {guideId}, {guideId, opposingId},
            {rail->OuterFirst, rail->OuterLast, rail->InnerLast, rail->InnerFirst}});
        rails.push_back(*rail);
    }
    for (std::size_t i = 0; i + 1 < rails.size(); ++i) {
        const auto& before = rails[i];
        const auto& after = rails[i + 1];
        const auto& guideBefore = *pairs[i].Guide->Shape;
        const auto& guideAfter = *pairs[i + 1].Guide->Shape;
        const auto& opposingBefore = *pairs[i + 1].Opposing->Shape;
        const auto& opposingAfter = *pairs[i].Opposing->Shape;
        const double guideTurn = Cross(Tangent(guideBefore, 1), Tangent(guideAfter, 0));
        const double opposingTurn = Cross(Tangent(opposingBefore, 1), Tangent(opposingAfter, 0));
        if (std::abs(guideTurn) <= AngularTolerance || std::abs(opposingTurn) <= AngularTolerance ||
            Sign(guideTurn) == Sign(opposingTurn))
            return std::nullopt;
        // Both skins can switch which one is convex along the chain.
        // The limits of the reciprocal finite sweeps bound each turn.
        std::vector<Point> polygon;
        for (Point point : {before.OuterLast, guideBefore.End, after.OuterFirst,
                after.InnerFirst, opposingBefore.End, before.InnerLast})
            if (polygon.empty() || Distance(polygon.back(), point) > model.Tolerance)
                polygon.push_back(point);
        if (polygon.size() > 1 && Distance(polygon.front(), polygon.back()) <= model.Tolerance)
            polygon.pop_back();
        if (polygon.size() < 3)
            return std::nullopt;
        plan.Walls.push_back({"转角区 " + std::to_string(i + 1), {},
            {guideBefore.Id, guideAfter.Id, opposingBefore.Id, opposingAfter.Id}, std::move(polygon)});
    }
    return plan;
}

std::optional<Plan> OpenPairedChainCoverage(const Model& model, const std::vector<GuideSpans>& traces)
{
    const auto pairsAcrossWall = [&](const Edge& guide, const Edge& opposing) {
        return &guide != &opposing && !guide.Shape->IsArc && !opposing.Shape->IsArc &&
            Dot(Inward(guide, 0), Inward(opposing, 0)) < -1 + AngularTolerance &&
            SweepsOverOpposingSide(model, guide, opposing, traces[&guide - model.Edges.data()]) &&
            SweepsOverOpposingSide(model, opposing, guide, traces[&opposing - model.Edges.data()]);
    };
    for (std::size_t i = 0; i < model.OuterCount; ++i) {
        const Edge* guide = &model.Edges[i];
        const Edge& startCap = AlongOuter(model, *guide, -1);
        const Edge* support = &AlongOuter(model, startCap, -1);
        if (startCap.Shape->IsArc)
            continue;
        std::vector<OpenRailPair> pairs;
        // Follow the two sides from the same actual end cap. A single
        // rail can sweep both to the other rail and beyond to a later cap.
        while (pairs.size() < model.OuterCount / 2 && pairsAcrossWall(*guide, *support)) {
            pairs.push_back({guide, support});
            guide = &AlongOuter(model, *guide, 1);
            support = &AlongOuter(model, *support, -1);
            if (guide != support)
                continue;
            // Each rail is consumed from an opposite end of the cyclic
            // contour. The two walks must meet at one distinct terminal cap.
            if (guide > &startCap && !guide->Shape->IsArc && pairs.size() * 2 + 2 == model.OuterCount)
                if (auto plan = OpenPairedChainPlan(model, pairs))
                    return plan;
            break;
        }
    }
    return std::nullopt;
}

std::optional<Plan> ProtrudingOpenWallCoverage(const Ring& source, const Model& original,
    const std::vector<GuideSpans>& originalTraces)
{
    auto branch = FindOpenWallBranch(original, originalTraces);
    if (!branch)
        return std::nullopt;
    std::vector<Ring> carrier{source};
    std::map<int, std::vector<int>> sources;
    for (const auto& curve : source.Curves)
        sources.emplace(curve.Id, std::vector<int>{curve.Id});
    std::vector<Wall> branches;
    const auto restoreSources = [&](Wall& wall) {
        std::vector<int> ids;
        for (int id : wall.SupportIds) {
            const auto& originals = sources.at(id);
            ids.insert(ids.end(), originals.begin(), originals.end());
        }
        wall.SupportIds = std::move(ids);
    };
    for (;;) {
        auto& curves = carrier.front().Curves;
        const std::size_t first = branch->Shoulder;
        const std::size_t last = (first + 4) % curves.size();
        restoreSources(branch->Region);
        branch->Region.Label += " " + std::to_string(branches.size() + 1);
        branches.push_back(std::move(branch->Region));
        const auto& continuation = sources.at(curves[last].Id);
        auto& remaining = sources.at(curves[first].Id);
        remaining.insert(remaining.end(), continuation.begin(), continuation.end());
        curves[first].End = curves[last].End;
        // Remove only the three branch trims and the second shoulder from
        // the discovery carrier. The branch face uses the original material.
        std::vector<Curve> reduced;
        for (std::size_t i = 0; i < curves.size(); ++i) {
            const std::size_t offset = (i + curves.size() - first) % curves.size();
            if (offset == 0 || offset > 4)
                reduced.push_back(curves[i]);
        }
        curves = std::move(reduced);
        const auto model = Prepare(carrier, original.Tolerance);
        if (!model)
            return std::nullopt;
        std::vector<GuideSpans> traces;
        for (const auto& edge : model->Edges)
            traces.push_back(Trace(*model, edge));
        branch = FindOpenWallBranch(*model, traces);
        if (branch)
            continue;
        auto plan = OpenCornerCoverage(*model, traces);
        if (!plan)
            plan = BranchedCoverage(*model, traces);
        if (!plan)
            plan = OpenPairedChainCoverage(*model, traces);
        if (!plan)
            return std::nullopt;
        for (auto& wall : plan->Walls)
            restoreSources(wall);
        for (auto& wall : branches)
            plan->Walls.push_back(std::move(wall));
        return plan;
    }
}
