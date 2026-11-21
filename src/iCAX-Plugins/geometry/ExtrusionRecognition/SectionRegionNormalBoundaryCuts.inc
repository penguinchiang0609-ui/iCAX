std::optional<std::vector<std::array<Point, 2>>> PlanNormalBoundaryCuts(
    const std::vector<Ring>& rings, double tolerance)
{
    const auto model = Prepare(rings, tolerance);
    if (!model || model->OuterCount < 3)
        return std::nullopt;
    const auto edgeAt = [&](std::size_t i) -> const Edge& { return model->Edges[i % model->OuterCount]; };
    const auto isCap = [&](std::size_t i)
    {
        const auto &prior = edgeAt(i + model->OuterCount - 1), &edge = edgeAt(i), &next = edgeAt(i + 1);
        if (prior.Shape->IsArc || edge.Shape->IsArc || next.Shape->IsArc)
            return false;
        const auto end = Tangent(*prior.Shape, 1), begin = Tangent(*next.Shape, 0);
        return Dot(end, begin) < AngularTolerance - 1 &&
            std::abs(Dot(end, Tangent(*edge.Shape, 0))) <= AngularTolerance;
    };
    const auto stepNormal = [&](std::size_t i) -> std::optional<Point>
    {
        const auto &prior = edgeAt(i + model->OuterCount - 1), &edge = edgeAt(i), &next = edgeAt(i + 1);
        if (prior.Shape->IsArc || edge.Shape->IsArc || next.Shape->IsArc ||
            CurveLength(*edge.Shape) >= std::min(CurveLength(*prior.Shape), CurveLength(*next.Shape)))
            return std::nullopt;
        const auto end = Tangent(*prior.Shape, 1), begin = Tangent(*next.Shape, 0);
        if (Dot(end, begin) < 1 - AngularTolerance || std::abs(Cross(end, begin)) > AngularTolerance ||
            std::abs(Cross(end, edge.Shape->End - edge.Shape->Start)) <= tolerance)
            return std::nullopt;
        return Inward(prior, 1);
    };

    std::vector<std::array<Point, 2>> cuts;
    for (std::size_t i = 0; i < model->OuterCount; ++i)
    {
        const auto &edge = edgeAt(i), &next = edgeAt(i + 1);
        const auto normal = Inward(edge, 1);
        if (Dot(normal, Inward(next, 0)) <= AngularTolerance)
        {
            if (isCap(i) || isCap(i + 1))
                continue;
            return std::nullopt;
        }
        // Parallel rails on either side of a short step fix the same sweep
        // direction at both ends. The step normal can cross the adjoining
        // rail cut and split material away from both guide regions.
        const auto intoMaterial = stepNormal(i).value_or(stepNormal(i + 1).value_or(normal));
        const auto start = Position(*edge.Shape, 1);
        auto hit = FirstExit(*model, start, intoMaterial);
        if (!hit || hit->Outward * hit->Distance <= tolerance)
            return std::nullopt;
        // A hole can end at a cusp where the ray touches both incident
        // trims without entering the void. The nearer finite support is
        // sufficient to terminate a partition cut at that branch root.
        for (std::size_t index = model->OuterCount; index < model->Edges.size(); ++index)
        {
            const auto& rail = model->Edges[index];
            VisitIntersections(*model, rail, start, intoMaterial, [&](const Hit& contact)
            {
                const auto& shape = *rail.Shape;
                if (contact.Distance > tolerance && contact.Distance < hit->Distance - tolerance &&
                    contact.Outward * contact.Distance > tolerance &&
                    std::min(Distance(contact.Position, shape.Start), Distance(contact.Position, shape.End)) <= tolerance)
                    hit = contact;
            });
        }
        const auto direction = hit->Position - start;
        for (const auto& cut : cuts)
        {
            const Point other = cut[1] - cut[0], offset = cut[0] - start;
            const double determinant = Cross(direction, other);
            if (std::abs(determinant) <= AngularTolerance * Length(direction) * Length(other))
                continue;
            const double a = Cross(offset, other) / determinant, b = Cross(offset, direction) / determinant;
            const double aSlack = tolerance / Length(direction), bSlack = tolerance / Length(other);
            if (a > aSlack && a < 1 - aSlack && b > bSlack && b < 1 - bSlack)
                return std::nullopt;
        }
        cuts.push_back({start, hit->Position});
    }
    return cuts.empty() ? std::nullopt : std::optional{std::move(cuts)};
}
