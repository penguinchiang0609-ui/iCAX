#include "pch.h"
#include "SecurityWindow2Template.h"

namespace
{
    using namespace iCAX::TubeDesigner;

    void ValidateProfile(const STubeProfile& Profile_, const std::string& Name_)
    {
        if (Profile_.Type != "rect")
        {
            throw std::invalid_argument(Name_ + " profile type is not supported");
        }
        if (!std::isfinite(Profile_.Width)
            || !std::isfinite(Profile_.Depth)
            || !std::isfinite(Profile_.WallThickness)
            || Profile_.Width <= 0.0
            || Profile_.Depth <= 0.0
            || Profile_.WallThickness <= 0.0
            || Profile_.WallThickness * 2.0 >= std::min(Profile_.Width, Profile_.Depth))
        {
            throw std::invalid_argument(Name_ + " profile dimensions are invalid");
        }
    }

    std::string MakePartNumber(const std::string& ProductCode_, std::uint64_t Index_)
    {
        std::ostringstream _Stream;
        _Stream << ProductCode_ << '-' << std::setw(3) << std::setfill('0') << Index_;
        return _Stream.str();
    }

    SGeneratedPart MakePart(
        std::uint64_t Index_, const std::string& ProductCode_, const std::string& Role_,
        double X1_, double Y1_, double X2_, double Y2_, const STubeProfile& Profile_)
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

    std::vector<double> PositionsInside(
        std::uint64_t Count_, double Bottom_, double Top_)
    {
        if (Count_ == 0) return {};
        if (Top_ <= Bottom_) throw std::invalid_argument("handle region height must be positive");
        const auto _Step = (Top_ - Bottom_) / static_cast<double>(Count_ + 1);
        std::vector<double> _Result;
        _Result.reserve(static_cast<std::size_t>(Count_));
        for (std::uint64_t _Index = 0; _Index < Count_; ++_Index)
        {
            _Result.push_back(Top_ - _Step * static_cast<double>(_Index + 1));
        }
        return _Result;
    }

    double ProfileSize(const SGeneratedPart& Part_)
    {
        return std::max(Part_.Profile.Width, Part_.Profile.Depth);
    }
}

iCAX::TubeDesigner::SGeneratedProduct iCAX::TubeDesigner::GenerateSecurityWindow2(
    const SSecurityWindow2Parameters& Parameters_)
{
    if (Parameters_.ProductCode.empty()) throw std::invalid_argument("product code is required");
    if (!std::isfinite(Parameters_.Height) || !std::isfinite(Parameters_.Width)
        || Parameters_.Height <= 0.0 || Parameters_.Width <= 0.0)
    {
        throw std::invalid_argument("product width and height must be positive");
    }
    if (!std::isfinite(Parameters_.HandleBottom)
        || !std::isfinite(Parameters_.HandleHeight)
        || !std::isfinite(Parameters_.HandleWidth)
        || Parameters_.HandleBottom < 0.0
        || Parameters_.HandleHeight <= 0.0
        || Parameters_.HandleBottom + Parameters_.HandleHeight > Parameters_.Height
        || Parameters_.HandleWidth <= 0.0
        || Parameters_.HandleWidth >= Parameters_.Width)
    {
        throw std::invalid_argument("handle dimensions must remain inside the product");
    }
    if (Parameters_.HandleHorizontalCount > 100
        || Parameters_.TopHorizontalCount > 100
        || Parameters_.BottomHorizontalCount > 100)
    {
        throw std::invalid_argument("horizontal count must be between 0 and 100");
    }
    if (!std::isfinite(Parameters_.AssemblyClearance)
        || Parameters_.AssemblyClearance < 0.0)
    {
        throw std::invalid_argument("assembly clearance cannot be negative");
    }
    ValidateProfile(Parameters_.Frame, "frame");
    ValidateProfile(Parameters_.Vertical, "vertical");
    ValidateProfile(Parameters_.Horizontal, "horizontal");
    if (Parameters_.Horizontal.Width <= Parameters_.Vertical.Width
        || Parameters_.Frame.Width <= Parameters_.Horizontal.Width)
    {
        throw std::invalid_argument("tube profile sizes must follow frame > horizontal > vertical");
    }
    if (Parameters_.Width <= Parameters_.Frame.Width * 2.0)
    {
        throw std::invalid_argument("product width must be greater than twice the frame width");
    }

    const auto _HorizontalReserve = Parameters_.HorizontalBranchReserve < 0.0
        ? Parameters_.Frame.Width / 2.0 : Parameters_.HorizontalBranchReserve;
    const auto _VerticalReserve = Parameters_.VerticalBranchReserve < 0.0
        ? Parameters_.Frame.Width / 2.0 : Parameters_.VerticalBranchReserve;
    if (_HorizontalReserve > Parameters_.Frame.Width
        || _VerticalReserve > Parameters_.Frame.Width)
    {
        throw std::invalid_argument("branch reserve cannot exceed frame width");
    }

    SGeneratedProduct _Result;
    _Result.ProductCode = Parameters_.ProductCode;
    _Result.TemplateID = "security-window-2";
    _Result.TemplateVersion = "1.0.0";
    _Result.Width = Parameters_.Width;
    _Result.Height = Parameters_.Height;

    const auto _HandleTop = Parameters_.HandleBottom + Parameters_.HandleHeight;
    const auto _HandleLeft = (Parameters_.Width - Parameters_.HandleWidth) / 2.0;
    const auto _HandleRight = _HandleLeft + Parameters_.HandleWidth;
    const auto _BranchStartX = Parameters_.Frame.Width - _HorizontalReserve;
    const auto _BranchEndX = Parameters_.Width - Parameters_.Frame.Width + _HorizontalReserve;
    const auto _LeftFrameX = Parameters_.Frame.Width / 2.0;
    const auto _RightFrameX = Parameters_.Width - Parameters_.Frame.Width / 2.0;

    _Result.Parts.push_back(MakePart(
        1, Parameters_.ProductCode, "left-frame", _LeftFrameX, 0.0,
        _LeftFrameX, Parameters_.Height, Parameters_.Frame));
    _Result.Parts.push_back(MakePart(
        2, Parameters_.ProductCode, "right-frame", _RightFrameX, 0.0,
        _RightFrameX, Parameters_.Height, Parameters_.Frame));

    std::uint64_t _PartIndex = 3;
    _Result.Parts.push_back(MakePart(
        _PartIndex++, Parameters_.ProductCode, "handle-left-vertical",
        _HandleLeft, Parameters_.HandleBottom, _HandleLeft, _HandleTop, Parameters_.Vertical));
    _Result.Parts.push_back(MakePart(
        _PartIndex++, Parameters_.ProductCode, "handle-right-vertical",
        _HandleRight, Parameters_.HandleBottom, _HandleRight, _HandleTop, Parameters_.Vertical));
    _Result.Parts.push_back(MakePart(
        _PartIndex++, Parameters_.ProductCode, "handle-top-horizontal",
        _HandleLeft, _HandleTop, _HandleRight, _HandleTop, Parameters_.Horizontal));
    _Result.Parts.push_back(MakePart(
        _PartIndex++, Parameters_.ProductCode, "handle-bottom-horizontal",
        _HandleLeft, Parameters_.HandleBottom, _HandleRight, Parameters_.HandleBottom, Parameters_.Horizontal));

    for (const auto _Y : PositionsInside(
        Parameters_.TopHorizontalCount, _HandleTop, Parameters_.Height))
    {
        _Result.Parts.push_back(MakePart(
            _PartIndex++, Parameters_.ProductCode, "top-area-horizontal",
            _BranchStartX, _Y, _BranchEndX, _Y, Parameters_.Horizontal));
    }
    for (const auto _Y : PositionsInside(
        Parameters_.HandleHorizontalCount, Parameters_.HandleBottom, _HandleTop))
    {
        _Result.Parts.push_back(MakePart(
            _PartIndex++, Parameters_.ProductCode, "handle-area-horizontal",
            _HandleLeft, _Y, _HandleRight, _Y, Parameters_.Horizontal));
    }
    for (const auto _Y : PositionsInside(
        Parameters_.BottomHorizontalCount, 0.0, Parameters_.HandleBottom))
    {
        _Result.Parts.push_back(MakePart(
            _PartIndex++, Parameters_.ProductCode, "bottom-area-horizontal",
            _BranchStartX, _Y, _BranchEndX, _Y, Parameters_.Horizontal));
    }

    for (const auto& _Vertical : _Result.Parts)
    {
        if (!_Vertical.IsVertical() || _Vertical.Role.starts_with("handle-")) continue;
        const auto _VerticalX = (_Vertical.X1 + _Vertical.X2) / 2.0;
        for (const auto& _Horizontal : _Result.Parts)
        {
            if (_Horizontal.IsVertical() || _Horizontal.Role.starts_with("handle-")) continue;
            const auto _HorizontalY = (_Horizontal.Y1 + _Horizontal.Y2) / 2.0;
            if (_VerticalX < std::min(_Horizontal.X1, _Horizontal.X2)
                || _VerticalX > std::max(_Horizontal.X1, _Horizontal.X2)
                || _HorizontalY < std::min(_Vertical.Y1, _Vertical.Y2)
                || _HorizontalY > std::max(_Vertical.Y1, _Vertical.Y2)) continue;
            const auto _HorizontalIsTarget = ProfileSize(_Horizontal) >= ProfileSize(_Vertical);
            _Result.Joints.push_back({
                _HorizontalIsTarget ? _Horizontal.Index : _Vertical.Index,
                _HorizontalIsTarget ? _Vertical.Index : _Horizontal.Index,
                "through", Parameters_.AssemblyClearance, _VerticalX, _HorizontalY
            });
        }
    }
    return _Result;
}
