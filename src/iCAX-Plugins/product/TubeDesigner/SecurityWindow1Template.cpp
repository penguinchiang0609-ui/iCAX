#include "pch.h"
#include "SecurityWindow1Template.h"

namespace
{
    using namespace iCAX::TubeDesigner;

    void ValidateProfile(const STubeProfile& Profile_, const std::string& Name_)
    {
        if (Profile_.Type != "rect")
        {
            throw std::invalid_argument(Name_ + " profile type is not supported in TubeDesigner P0");
        }
        if (!std::isfinite(Profile_.Width)
            || !std::isfinite(Profile_.Depth)
            || !std::isfinite(Profile_.WallThickness)
            || Profile_.Width <= 0.0
            || Profile_.Depth <= 0.0)
        {
            throw std::invalid_argument(Name_ + " profile dimensions must be positive");
        }
        if (Profile_.WallThickness <= 0.0
            || Profile_.WallThickness * 2.0 >= std::min(Profile_.Width, Profile_.Depth))
        {
            throw std::invalid_argument(Name_ + " wall thickness must be smaller than half of the tube size");
        }
    }

    std::string MakePartNumber(const std::string& ProductCode_, std::uint64_t Index_)
    {
        std::ostringstream _Stream;
        _Stream << ProductCode_ << '-' << std::setw(3) << std::setfill('0') << Index_;
        return _Stream.str();
    }

    SGeneratedPart MakePart(
        std::uint64_t Index_,
        const std::string& ProductCode_,
        const std::string& Role_,
        double X1_, double Y1_, double X2_, double Y2_,
        const STubeProfile& Profile_)
    {
        SGeneratedPart _Part;
        _Part.Index = Index_;
        _Part.PartNumber = MakePartNumber(ProductCode_, Index_);
        _Part.Role = Role_;
        _Part.Profile = Profile_;
        _Part.X1 = X1_;
        _Part.Y1 = Y1_;
        _Part.X2 = X2_;
        _Part.Y2 = Y2_;
        _Part.Length = std::hypot(X2_ - X1_, Y2_ - Y1_);
        return _Part;
    }

    double ProfileSize(const SGeneratedPart& Part_)
    {
        return std::max(Part_.Profile.Width, Part_.Profile.Depth);
    }
}

iCAX::TubeDesigner::SGeneratedProduct iCAX::TubeDesigner::GenerateSecurityWindow1(
    const SSecurityWindow1Parameters& Parameters_)
{
    if (Parameters_.ProductCode.empty())
    {
        throw std::invalid_argument("product code is required");
    }
    if (!std::isfinite(Parameters_.Height)
        || !std::isfinite(Parameters_.Width)
        || Parameters_.Height <= 0.0
        || Parameters_.Width <= 0.0)
    {
        throw std::invalid_argument("product width and height must be positive");
    }
    if (Parameters_.MiddleVerticalCount > 10)
    {
        throw std::invalid_argument("middle vertical count must be between 0 and 10");
    }
    if (Parameters_.HorizontalCount > 100)
    {
        throw std::invalid_argument("horizontal count must be between 0 and 100");
    }
    if (!std::isfinite(Parameters_.FirstHorizontalTopOffset)
        || !std::isfinite(Parameters_.LastHorizontalBottomOffset)
        || Parameters_.FirstHorizontalTopOffset < 0.0
        || Parameters_.LastHorizontalBottomOffset < 0.0
        || Parameters_.FirstHorizontalTopOffset > Parameters_.Height
        || Parameters_.LastHorizontalBottomOffset > Parameters_.Height)
    {
        throw std::invalid_argument("horizontal offsets must be inside the product height");
    }
    if (!std::isfinite(Parameters_.HorizontalBranchReserve)
        || !std::isfinite(Parameters_.VerticalBranchReserve)
        || (Parameters_.HorizontalBranchReserve < 0.0
            && Parameters_.HorizontalBranchReserve != -1.0)
        || (Parameters_.VerticalBranchReserve < 0.0
            && Parameters_.VerticalBranchReserve != -1.0))
    {
        throw std::invalid_argument("branch reserve must be -1 or non-negative");
    }
    if (!std::isfinite(Parameters_.AssemblyClearance)
        || Parameters_.AssemblyClearance < 0.0)
    {
        throw std::invalid_argument("assembly clearance cannot be negative");
    }

    ValidateProfile(Parameters_.Frame, "frame");
    ValidateProfile(Parameters_.Vertical, "vertical");
    ValidateProfile(Parameters_.Horizontal, "horizontal");

    if (Parameters_.Horizontal.Width <= Parameters_.Vertical.Width)
    {
        throw std::invalid_argument("horizontal tube width must be greater than vertical tube width");
    }
    if (Parameters_.Frame.Width <= Parameters_.Horizontal.Width)
    {
        throw std::invalid_argument("frame tube width must be greater than horizontal tube width");
    }
    if (Parameters_.Width <= Parameters_.Frame.Width * 2.0)
    {
        throw std::invalid_argument("product width must be greater than twice the frame tube width");
    }
    if (Parameters_.Height <= Parameters_.Frame.Width)
    {
        throw std::invalid_argument("product height must be greater than the frame tube width");
    }

    const auto _HorizontalReserve = Parameters_.HorizontalBranchReserve < 0.0
        ? Parameters_.Frame.Width / 2.0
        : Parameters_.HorizontalBranchReserve;
    const auto _VerticalReserve = Parameters_.VerticalBranchReserve < 0.0
        ? Parameters_.Frame.Width / 2.0
        : Parameters_.VerticalBranchReserve;
    if (_HorizontalReserve > Parameters_.Frame.Width
        || _VerticalReserve > Parameters_.Frame.Width)
    {
        throw std::invalid_argument("branch reserve cannot exceed frame tube width");
    }

    std::vector<double> _HorizontalY;
    if (Parameters_.HorizontalCount == 1)
    {
        _HorizontalY.push_back(
            (Parameters_.Height - Parameters_.FirstHorizontalTopOffset
                + Parameters_.LastHorizontalBottomOffset) / 2.0);
    }
    else if (Parameters_.HorizontalCount > 1)
    {
        const auto _TopY = Parameters_.Height - Parameters_.FirstHorizontalTopOffset;
        const auto _BottomY = Parameters_.LastHorizontalBottomOffset;
        if (_TopY < _BottomY)
        {
            throw std::invalid_argument("horizontal offsets leave no valid spacing");
        }
        const auto _Step = (_TopY - _BottomY)
            / static_cast<double>(Parameters_.HorizontalCount - 1);
        for (std::uint64_t _Index = 0; _Index < Parameters_.HorizontalCount; ++_Index)
        {
            _HorizontalY.push_back(_TopY - static_cast<double>(_Index) * _Step);
        }
    }

    SGeneratedProduct _Result;
    _Result.ProductCode = Parameters_.ProductCode;
    _Result.Width = Parameters_.Width;
    _Result.Height = Parameters_.Height;

    const auto _LeftFrameX = Parameters_.Frame.Width / 2.0;
    const auto _RightFrameX = Parameters_.Width - Parameters_.Frame.Width / 2.0;
    const auto _InnerLeftX = Parameters_.Frame.Width;
    const auto _InnerRightX = Parameters_.Width - Parameters_.Frame.Width;
    const auto _BranchStartX = Parameters_.Frame.Width - _HorizontalReserve;
    const auto _BranchEndX = Parameters_.Width - Parameters_.Frame.Width + _HorizontalReserve;
    const auto _BranchStartY = Parameters_.Frame.Width - _VerticalReserve;
    const auto _BranchEndY = Parameters_.Height - Parameters_.Frame.Width + _VerticalReserve;
    if (Parameters_.MiddleVerticalCount > 0 && _BranchEndY <= _BranchStartY)
    {
        throw std::invalid_argument("product height leaves no valid middle vertical length");
    }

    _Result.Parts.push_back(MakePart(
        1, Parameters_.ProductCode, "left-frame",
        _LeftFrameX, 0.0, _LeftFrameX, Parameters_.Height, Parameters_.Frame));
    _Result.Parts.push_back(MakePart(
        2, Parameters_.ProductCode, "right-frame",
        _RightFrameX, 0.0, _RightFrameX, Parameters_.Height, Parameters_.Frame));

    std::uint64_t _PartIndex = 3;
    for (std::uint64_t _Index = 0; _Index < Parameters_.MiddleVerticalCount; ++_Index)
    {
        const auto _X = _InnerLeftX + (_InnerRightX - _InnerLeftX)
            * static_cast<double>(_Index + 1)
            / static_cast<double>(Parameters_.MiddleVerticalCount + 1);
        _Result.Parts.push_back(MakePart(
            _PartIndex++, Parameters_.ProductCode, "middle-vertical",
            _X, _BranchStartY, _X, _BranchEndY, Parameters_.Vertical));
    }
    for (const auto _Y : _HorizontalY)
    {
        _Result.Parts.push_back(MakePart(
            _PartIndex++, Parameters_.ProductCode, "horizontal",
            _BranchStartX, _Y, _BranchEndX, _Y, Parameters_.Horizontal));
    }

    for (const auto& _Vertical : _Result.Parts)
    {
        if (!_Vertical.IsVertical()) continue;
        const auto _VerticalX = (_Vertical.X1 + _Vertical.X2) / 2.0;
        const auto _VerticalMinY = std::min(_Vertical.Y1, _Vertical.Y2);
        const auto _VerticalMaxY = std::max(_Vertical.Y1, _Vertical.Y2);
        for (const auto& _Horizontal : _Result.Parts)
        {
            if (_Horizontal.IsVertical()) continue;
            const auto _HorizontalYValue = (_Horizontal.Y1 + _Horizontal.Y2) / 2.0;
            const auto _HorizontalMinX = std::min(_Horizontal.X1, _Horizontal.X2);
            const auto _HorizontalMaxX = std::max(_Horizontal.X1, _Horizontal.X2);
            if (_VerticalX < _HorizontalMinX || _VerticalX > _HorizontalMaxX
                || _HorizontalYValue < _VerticalMinY || _HorizontalYValue > _VerticalMaxY)
            {
                continue;
            }

            const auto _HorizontalIsTarget = ProfileSize(_Horizontal) >= ProfileSize(_Vertical);
            _Result.Joints.push_back({
                _HorizontalIsTarget ? _Horizontal.Index : _Vertical.Index,
                _HorizontalIsTarget ? _Vertical.Index : _Horizontal.Index,
                "through",
                Parameters_.AssemblyClearance,
                _VerticalX,
                _HorizontalYValue
            });
        }
    }
    return _Result;
}
