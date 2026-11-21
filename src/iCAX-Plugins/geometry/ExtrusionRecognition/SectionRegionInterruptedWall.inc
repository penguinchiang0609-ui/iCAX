struct InterruptedSupport
{
    std::size_t First;
    std::size_t Last;
};

bool ProvesWallInterruption(const Model& model, const std::vector<GuideSpans>& traces,
    std::size_t first, std::size_t last)
{
    const Edge& before = model.Edges[first];
    const Edge& after = model.Edges[last];
    if (!Collinear(model, before, after))
        return false;
    const Point origin = before.Shape->End;
    const Point along = Tangent(*before.Shape, 0);
    const Point inward = Inward(before, 0);
    const double gap = Dot(after.Shape->Start - origin, along);
    if (Dot(along, Tangent(*after.Shape, 0)) <= 0 || gap <= model.Tolerance ||
        std::abs(Dot(after.Shape->Start - origin, inward)) > model.Tolerance ||
        std::abs(Dot(after.Shape->End - origin, inward)) > model.Tolerance)
        return false;

    std::set<const Edge*> notch;
    double inset = 0;
    for (std::size_t index = (first + 1) % model.OuterCount; index != last;
        index = (index + 1) % model.OuterCount) {
        const Edge& edge = model.Edges[index];
        if (edge.Shape->IsArc)
            return false;
        notch.insert(&edge);
        for (const Point endpoint : {edge.Shape->Start, edge.Shape->End}) {
            const double parameter = Dot(endpoint - origin, along);
            const double depth = Dot(endpoint - origin, inward);
            if (parameter < -model.Tolerance || parameter > gap + model.Tolerance ||
                depth < -model.Tolerance)
                return false;
            inset = std::max(inset, depth);
        }
    }
    if (notch.empty() || inset <= model.Tolerance)
        return false;

    for (std::size_t index = 0; index < model.OuterCount; ++index) {
        const Edge& opposite = model.Edges[index];
        if (opposite.Shape->IsArc ||
            Dot(Inward(opposite, 0), inward) > -1 + AngularTolerance ||
            Dot(opposite.Shape->Start - origin, inward) <= inset + model.Tolerance ||
            Dot(opposite.Shape->End - origin, inward) <= inset + model.Tolerance)
            continue;
        bool beforeHit = false, afterHit = false, coversGap = true;
        double covered = 0;
        for (const auto& span : traces[index]) {
            const double a = Dot(Position(*opposite.Shape, span.First) - origin, along);
            const double b = Dot(Position(*opposite.Shape, span.Last) - origin, along);
            const double low = std::min(a, b), high = std::max(a, b);
            beforeHit |= span.Target == &before && low < -model.Tolerance && high >= -model.Tolerance;
            afterHit |= span.Target == &after && high > gap + model.Tolerance && low <= gap + model.Tolerance;
            const double length = std::min(high, gap) - std::max(low, 0.0);
            if (length <= model.Tolerance)
                continue;
            if (!span.Target || !notch.contains(span.Target)) {
                coversGap = false;
                break;
            }
            covered += length;
        }
        // The same real opposite wall sees both shoulders. Within the gap
        // every first exit is on the intervening inset, so the cutout is a
        // missing part of this wall's skin rather than a different wall.
        if (coversGap && beforeHit && afterHit && covered >= gap - model.Tolerance)
            return true;
    }
    return false;
}

std::optional<Plan> InterruptedOpenWallCoverage(const Ring& source, const Model& model,
    const std::vector<GuideSpans>& traces)
{
    std::vector<InterruptedSupport> interruptions;
    std::vector<bool> used(model.OuterCount), removed(model.OuterCount);
    Ring carrier = source;
    for (std::size_t first = 0; first < model.OuterCount; ++first) {
        if (used[first] || model.Edges[first].Shape->IsArc)
            continue;
        for (std::size_t last = (first + 1) % model.OuterCount;
            last != first && !used[last]; last = (last + 1) % model.OuterCount) {
            if (!ProvesWallInterruption(model, traces, first, last))
                continue;
            interruptions.push_back({first, last});
            carrier.Curves[first].End = carrier.Curves[last].End;
            used[first] = true;
            for (std::size_t index = (first + 1) % model.OuterCount; ; index = (index + 1) % model.OuterCount) {
                removed[index] = used[index] = true;
                if (index == last)
                    break;
            }
            break;
        }
    }
    if (interruptions.empty())
        return std::nullopt;
    Ring reduced;
    for (std::size_t index = 0; index < carrier.Curves.size(); ++index)
        if (!removed[index])
            reduced.Curves.push_back(carrier.Curves[index]);
    const std::vector<Ring> contour{std::move(reduced)};
    const auto modelWithoutNotches = Prepare(contour, model.Tolerance);
    if (!modelWithoutNotches)
        return std::nullopt;
    std::vector<GuideSpans> continuedTraces;
    for (const auto& side : modelWithoutNotches->Edges)
        continuedTraces.push_back(Trace(*modelWithoutNotches, side));
    auto plan = OpenCornerCoverage(*modelWithoutNotches, continuedTraces);
    if (!plan)
        plan = BranchedCoverage(*modelWithoutNotches, continuedTraces);
    if (!plan)
        return std::nullopt;
    for (auto& wall : plan->Walls)
        for (const auto& cutout : interruptions)
            if (std::ranges::find(wall.SupportIds, source.Curves[cutout.First].Id) != wall.SupportIds.end())
                for (std::size_t index = (cutout.First + 1) % model.OuterCount; ; index = (index + 1) % model.OuterCount) {
                    wall.SupportIds.push_back(source.Curves[index].Id);
                    if (index == cutout.Last)
                        break;
                }
    // Only the discovery carrier has closed the cutout. BuildMaterialCells
    // intersects the plan with the original, unchanged BREP material.
    return plan;
}
