#include "NestingInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
    namespace
    {
        bool FiniteRect(const Rect& rectangle)
        {
            return std::isfinite(rectangle.X) && std::isfinite(rectangle.Y)
                && std::isfinite(rectangle.Width) && std::isfinite(rectangle.Height)
                && std::isfinite(rectangle.X + rectangle.Width)
                && std::isfinite(rectangle.Y + rectangle.Height);
        }

        bool Contains(const Rect& outer, const Rect& inner)
        {
            // Pruning uses exact containment. Tolerant containment can discard two
            // nearly coincident rectangles that each contain a different sliver.
            return inner.X >= outer.X && inner.Y >= outer.Y
                && inner.X + inner.Width <= outer.X + outer.Width
                && inner.Y + inner.Height <= outer.Y + outer.Height;
        }

        bool Better(const RectFit& candidate, const RectFit& incumbent, double tolerance)
        {
            if (candidate.Primary < incumbent.Primary - tolerance) return true;
            if (candidate.Primary > incumbent.Primary + tolerance) return false;
            if (candidate.Secondary < incumbent.Secondary - tolerance) return true;
            if (candidate.Secondary > incumbent.Secondary + tolerance) return false;
            return std::tie(candidate.Box.Y, candidate.Box.X)
                < std::tie(incumbent.Box.Y, incumbent.Box.X);
        }
    }

    MaxRects::MaxRects(double width, double height, double tolerance)
        : Tolerance(std::isfinite(tolerance) && tolerance >= 0 ? tolerance : 0)
    {
        if (std::isfinite(width) && std::isfinite(height)
            && width > Tolerance && height > Tolerance)
            Free.push_back({0, 0, width, height});
    }

    std::optional<RectFit> MaxRects::Find(double width, double height, RectScore score) const
    {
        if (!std::isfinite(width) || !std::isfinite(height)
            || width <= Tolerance || height <= Tolerance)
            return std::nullopt;

        std::optional<RectFit> best;
        for (const auto& space : Free)
        {
            if (width > space.Width + Tolerance || height > space.Height + Tolerance)
                continue;

            const double remainingWidth = std::max(0.0, space.Width - width);
            const double remainingHeight = std::max(0.0, space.Height - height);
            const double shorter = std::min(remainingWidth, remainingHeight);
            const double longer = std::max(remainingWidth, remainingHeight);
            RectFit candidate{{space.X, space.Y, width, height}, 0, 0};
            switch (score)
            {
            case RectScore::BottomLeft:
                candidate.Primary = space.Y + height;
                candidate.Secondary = space.X;
                break;
            case RectScore::ShortSide:
                candidate.Primary = shorter;
                candidate.Secondary = longer;
                break;
            case RectScore::LongSide:
                candidate.Primary = longer;
                candidate.Secondary = shorter;
                break;
            case RectScore::Area:
                // Equivalent to free area minus occupied area, avoiding subtraction
                // of two nearly equal products when the fit is almost exact.
                candidate.Primary = remainingWidth * space.Height + width * remainingHeight;
                candidate.Secondary = shorter;
                break;
            }
            if (!best || Better(candidate, *best, Tolerance)) best = candidate;
        }
        return best;
    }

    void MaxRects::Occupy(const Rect& used)
    {
        if (!FiniteRect(used) || used.Width <= Tolerance || used.Height <= Tolerance)
            return;

        const double usedRight = used.X + used.Width;
        const double usedTop = used.Y + used.Height;
        std::vector<Rect> split;
        split.reserve(Free.size() * 2);
        const auto add = [&](double x, double y, double width, double height)
        {
            if (width > Tolerance && height > Tolerance)
                split.push_back({x, y, width, height});
        };

        // Every intersected free rectangle must be split. The free rectangles can
        // overlap each other; they are alternative placement spaces, not a tiling.
        for (const auto& space : Free)
        {
            const double right = space.X + space.Width;
            const double top = space.Y + space.Height;
            if (used.X >= right - Tolerance || usedRight <= space.X + Tolerance
                || used.Y >= top - Tolerance || usedTop <= space.Y + Tolerance)
            {
                split.push_back(space);
                continue;
            }
            if (used.X > space.X)
                add(space.X, space.Y, used.X - space.X, space.Height);
            if (usedRight < right)
                add(usedRight, space.Y, right - usedRight, space.Height);
            if (used.Y > space.Y)
                add(space.X, space.Y, space.Width, used.Y - space.Y);
            if (usedTop < top)
                add(space.X, usedTop, space.Width, top - usedTop);
        }

        // Remove duplicates and contained spaces without changing the union of
        // usable space. Erase the later duplicate to keep deterministic ordering.
        std::vector<bool> removed(split.size(), false);
        for (std::size_t i = 0; i < split.size(); ++i)
        {
            if (removed[i]) continue;
            for (std::size_t j = i + 1; j < split.size(); ++j)
            {
                if (removed[j]) continue;
                if (Contains(split[i], split[j])) removed[j] = true;
                else if (Contains(split[j], split[i]))
                {
                    removed[i] = true;
                    break;
                }
            }
        }
        Free.clear();
        Free.reserve(split.size());
        for (std::size_t i = 0; i < split.size(); ++i)
            if (!removed[i]) Free.push_back(split[i]);
    }
}
