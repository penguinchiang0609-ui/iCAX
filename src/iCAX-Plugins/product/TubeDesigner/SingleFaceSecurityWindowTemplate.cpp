#include "pch.h"
#include "SingleFaceSecurityWindowTemplate.h"

#include "SecurityWindow3Template.h"

namespace
{
    using namespace iCAX::TubeDesigner;

    constexpr double kGeometryTolerance = 1.0e-7;

    void ValidateProfile(const STubeProfile& Profile_, const std::string& Name_)
    {
        if (Profile_.Type != "rect" && Profile_.Type != "round")
            throw std::invalid_argument(Name_ + " profile type is not supported");
        if (!std::isfinite(Profile_.Width) || !std::isfinite(Profile_.Depth)
            || !std::isfinite(Profile_.WallThickness) || Profile_.Width <= 0.0
            || Profile_.Depth <= 0.0 || Profile_.WallThickness <= 0.0)
            throw std::invalid_argument(Name_ + " profile dimensions must be positive");
        const auto _LimitingSize = Profile_.Type == "round"
            ? Profile_.Width : std::min(Profile_.Width, Profile_.Depth);
        if (Profile_.WallThickness * 2.0 >= _LimitingSize)
            throw std::invalid_argument(Name_ + " wall thickness is too large");
        if (!std::isfinite(Profile_.CornerRadius) || Profile_.CornerRadius < 0.0
            || (Profile_.Type == "rect" && Profile_.CornerRadius >= _LimitingSize / 2.0))
            throw std::invalid_argument(Name_ + " corner radius is invalid");
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

    double ProfileSize(const SGeneratedPart& Part_)
    {
        return Part_.Profile.Type == "round"
            ? Part_.Profile.Width
            : std::max(Part_.Profile.Width, Part_.Profile.Depth);
    }

    bool IsFrameRole(const std::string& Role_)
    {
        return Role_ == "outer-frame" || Role_.ends_with("-frame");
    }

    bool IsDoorLeafRole(const std::string& Role_)
    {
        return Role_.starts_with("access-door-leaf-");
    }

    std::vector<double> EvenPositions(std::uint64_t Count_, double Minimum_, double Maximum_)
    {
        if (Count_ == 0) return {};
        if (Maximum_ <= Minimum_) throw std::invalid_argument("bar layout has no usable span");
        std::vector<double> _Result;
        _Result.reserve(static_cast<std::size_t>(Count_));
        const auto _Step = (Maximum_ - Minimum_) / static_cast<double>(Count_ + 1);
        for (std::uint64_t _Index = 0; _Index < Count_; ++_Index)
            _Result.push_back(Minimum_ + _Step * static_cast<double>(_Index + 1));
        return _Result;
    }

    std::vector<double> HorizontalPositions(
        const SSingleFaceSecurityWindowParameters& Parameters_)
    {
        if (Parameters_.HorizontalCount == 0) return {};
        const auto _Top = Parameters_.Height - Parameters_.FirstHorizontalTopOffset;
        const auto _Bottom = Parameters_.LastHorizontalBottomOffset;
        if (_Top < _Bottom)
            throw std::invalid_argument("horizontal offsets leave no valid spacing");
        if (Parameters_.HorizontalCount == 1) return { (_Top + _Bottom) / 2.0 };
        std::vector<double> _Result;
        _Result.reserve(static_cast<std::size_t>(Parameters_.HorizontalCount));
        const auto _Step = (_Top - _Bottom)
            / static_cast<double>(Parameters_.HorizontalCount - 1);
        for (std::uint64_t _Index = 0; _Index < Parameters_.HorizontalCount; ++_Index)
            _Result.push_back(_Top - _Step * static_cast<double>(_Index));
        return _Result;
    }

    void AddPart(
        SGeneratedProduct& Product_, const std::string& Role_,
        double X1_, double Y1_, double X2_, double Y2_, const STubeProfile& Profile_)
    {
        if (std::hypot(X2_ - X1_, Y2_ - Y1_) <= kGeometryTolerance) return;
        const auto _Index = static_cast<std::uint64_t>(Product_.Parts.size() + 1);
        Product_.Parts.push_back(MakePart(
            _Index, Product_.ProductCode, Role_, X1_, Y1_, X2_, Y2_, Profile_));
    }

    void AssignPreviewAssembly(
        SGeneratedProduct& Product_, std::uint64_t FirstPartIndex_,
        const std::string& Key_, const std::string& Role_, const std::string& Name_)
    {
        for (auto& _Part : Product_.Parts)
        {
            if (_Part.Index < FirstPartIndex_) continue;
            _Part.PreviewAssemblyKey = Key_;
            _Part.PreviewAssemblyRole = Role_;
            _Part.PreviewAssemblyName = Name_;
        }
    }

    void ApplyTubeCornerProcess(
        SSecurityWindow3Parameters& Target_, const STubeCornerProcessParameters& Process_)
    {
        Target_.FrameJoinType = Process_.JoinType;
        Target_.FrameButtWrapMode = Process_.ButtWrapMode;
        Target_.VGrooveStyle = Process_.VGrooveStyle;
        Target_.VGrooveBottomDistance = Process_.VGrooveBottomDistance;
        Target_.VGrooveRadius = Process_.VGrooveRadius;
        Target_.VGrooveKFactor = Process_.VGrooveKFactor;
        Target_.VGrooveMaleFemale = Process_.VGrooveMaleFemale;
        Target_.VGrooveBottomCut = Process_.VGrooveBottomCut;
        Target_.VGrooveReliefHole = Process_.VGrooveReliefHole;
        Target_.VGrooveWallOvercut = Process_.VGrooveWallOvercut;
        Target_.VGrooveReliefDiameter = Process_.VGrooveReliefDiameter;
        Target_.VGrooveReliefNoThrough = Process_.VGrooveReliefNoThrough;
    }

    void AddFourSideOuterFrame(
        SGeneratedProduct& Product_, const SSingleFaceSecurityWindowParameters& Parameters_)
    {
        SSecurityWindow3Parameters _FrameParameters;
        _FrameParameters.ProductCode = Parameters_.ProductCode;
        _FrameParameters.Height = Parameters_.Height;
        _FrameParameters.Width = Parameters_.Width;
        ApplyTubeCornerProcess(_FrameParameters, Parameters_.OuterFrameProcess);
        _FrameParameters.HorizontalCount = 0;
        _FrameParameters.MiddleVerticalCount = 0;
        _FrameParameters.FirstHorizontalTopOffset = Parameters_.FirstHorizontalTopOffset;
        _FrameParameters.LastHorizontalBottomOffset = Parameters_.LastHorizontalBottomOffset;
        _FrameParameters.HorizontalBranchReserve = Parameters_.HorizontalBranchReserve;
        _FrameParameters.VerticalBranchReserve = Parameters_.VerticalBranchReserve;
        _FrameParameters.AssemblyClearance = Parameters_.AssemblyClearance;
        _FrameParameters.Frame = Parameters_.Frame;
        _FrameParameters.Horizontal = Parameters_.Horizontal;
        _FrameParameters.Vertical = { "rect", Parameters_.Vertical.Width,
            Parameters_.Vertical.Width, Parameters_.Vertical.WallThickness };
        auto _Frame = GenerateSecurityWindow3(_FrameParameters);
        Product_.Parts = std::move(_Frame.Parts);
    }

    struct SRectangularTubeAssemblyIndices final
    {
        std::uint64_t First = 0;
        std::uint64_t Left = 0;
        std::uint64_t Right = 0;
    };

    // A rectangle is only one consumer of the reusable tube-corner process catalog.
    // L/U/polyline tube paths can use the same process parameters without becoming
    // a special kind of "closed-frame process".
    SRectangularTubeAssemblyIndices AddRectangularTubeAssembly(
        SGeneratedProduct& Product_, const STubeCornerProcessParameters& Process_,
        const std::string& RolePrefix_, const std::string& AssemblyKey_,
        const std::string& AssemblyRole_, const std::string& AssemblyName_,
        double Left_, double Bottom_, double Right_, double Top_, const STubeProfile& Profile_)
    {
        SSecurityWindow3Parameters _FrameParameters;
        _FrameParameters.ProductCode = Product_.ProductCode;
        _FrameParameters.Height = Top_ - Bottom_;
        _FrameParameters.Width = Right_ - Left_;
        ApplyTubeCornerProcess(_FrameParameters, Process_);
        _FrameParameters.HorizontalCount = 0;
        _FrameParameters.MiddleVerticalCount = 0;
        _FrameParameters.Frame = Profile_;
        const auto _DummyHorizontalSize = Profile_.Width * 0.6;
        const auto _DummyVerticalSize = Profile_.Width * 0.3;
        const auto _DummyWall = std::min(
            Profile_.WallThickness, _DummyVerticalSize * 0.2);
        _FrameParameters.Horizontal = {
            "rect", _DummyHorizontalSize, _DummyHorizontalSize, _DummyWall
        };
        _FrameParameters.Vertical = {
            "rect", _DummyVerticalSize, _DummyVerticalSize, _DummyWall
        };

        auto _Frame = GenerateSecurityWindow3(_FrameParameters);
        if (_Frame.Parts.empty())
            throw std::runtime_error("processed closed frame generated no parts");
        const auto _First = static_cast<std::uint64_t>(Product_.Parts.size() + 1);
        for (auto& _Part : _Frame.Parts)
        {
            _Part.Index = static_cast<std::uint64_t>(Product_.Parts.size() + 1);
            _Part.PartNumber = MakePartNumber(Product_.ProductCode, _Part.Index);
            if (_Part.Role == "outer-frame") _Part.Role = RolePrefix_ + "frame";
            else _Part.Role = RolePrefix_ + _Part.Role;
            _Part.X1 += Left_;
            _Part.X2 += Left_;
            _Part.Y1 += Bottom_;
            _Part.Y2 += Bottom_;
            for (auto& _Point : _Part.PathPoints)
            {
                _Point.X += Left_;
                _Point.Y += Bottom_;
            }
            if (_Part.PreviewGeometry)
            {
                _Part.PreviewGeometry->OuterMinX += Left_;
                _Part.PreviewGeometry->OuterMaxX += Left_;
                _Part.PreviewGeometry->OpeningMinX += Left_;
                _Part.PreviewGeometry->OpeningMaxX += Left_;
                _Part.PreviewGeometry->OuterMinY += Bottom_;
                _Part.PreviewGeometry->OuterMaxY += Bottom_;
                _Part.PreviewGeometry->OpeningMinY += Bottom_;
                _Part.PreviewGeometry->OpeningMaxY += Bottom_;
            }
            Product_.Parts.push_back(std::move(_Part));
        }
        AssignPreviewAssembly(
            Product_, _First, AssemblyKey_, AssemblyRole_, AssemblyName_);

        if (Process_.JoinType != "v_groove_90")
        {
            Product_.Joints.push_back({ _First, _First + 3, "weld", 0.0, Left_, Bottom_, RolePrefix_ + "corner/lower-left" });
            Product_.Joints.push_back({ _First, _First + 2, "weld", 0.0, Left_, Top_, RolePrefix_ + "corner/upper-left" });
            Product_.Joints.push_back({ _First + 1, _First + 3, "weld", 0.0, Right_, Bottom_, RolePrefix_ + "corner/lower-right" });
            Product_.Joints.push_back({ _First + 1, _First + 2, "weld", 0.0, Right_, Top_, RolePrefix_ + "corner/upper-right" });
        }
        return {
            _First,
            _First,
            Process_.JoinType == "v_groove_90" ? _First : _First + 1
        };
    }

    void AddOuterFrame(
        SGeneratedProduct& Product_, const SSingleFaceSecurityWindowParameters& Parameters_)
    {
        const auto _Half = Parameters_.Frame.Width / 2.0;
        if (Parameters_.FrameLayout == "left_right")
        {
            AddPart(Product_, "left-frame", _Half, 0.0, _Half, Parameters_.Height, Parameters_.Frame);
            AddPart(Product_, "right-frame", Parameters_.Width - _Half, 0.0,
                Parameters_.Width - _Half, Parameters_.Height, Parameters_.Frame);
        }
        else if (Parameters_.FrameLayout == "top_bottom")
        {
            AddPart(Product_, "bottom-frame", 0.0, _Half, Parameters_.Width, _Half, Parameters_.Frame);
            AddPart(Product_, "top-frame", 0.0, Parameters_.Height - _Half,
                Parameters_.Width, Parameters_.Height - _Half, Parameters_.Frame);
        }
        else if (Parameters_.FrameLayout == "four_sides")
        {
            AddFourSideOuterFrame(Product_, Parameters_);
        }
        else
        {
            throw std::invalid_argument("unsupported single-face security window frame layout: "
                + Parameters_.FrameLayout);
        }
    }

    void AddMainGrid(
        SGeneratedProduct& Product_, const SSingleFaceSecurityWindowParameters& Parameters_)
    {
        const auto _HorizontalReserve = Parameters_.HorizontalBranchReserve < 0.0
            ? Parameters_.Frame.Width / 2.0 : Parameters_.HorizontalBranchReserve;
        const auto _VerticalReserve = Parameters_.VerticalBranchReserve < 0.0
            ? Parameters_.Frame.Width / 2.0 : Parameters_.VerticalBranchReserve;
        const auto _HasSideFrame = Parameters_.FrameLayout != "top_bottom";
        const auto _HorizontalStart = _HasSideFrame
            ? Parameters_.Frame.Width - _HorizontalReserve : Parameters_.Frame.Width / 2.0;
        const auto _HorizontalEnd = _HasSideFrame
            ? Parameters_.Width - Parameters_.Frame.Width + _HorizontalReserve
            : Parameters_.Width - Parameters_.Frame.Width / 2.0;
        const auto _VerticalStart = Parameters_.Frame.Width - _VerticalReserve;
        const auto _VerticalEnd = Parameters_.Height - Parameters_.Frame.Width + _VerticalReserve;
        const auto _DoorRight = Parameters_.DoorLeft + Parameters_.DoorWidth;
        const auto _DoorTop = Parameters_.DoorBottom + Parameters_.DoorHeight;

        for (const auto _Y : HorizontalPositions(Parameters_))
        {
            if (Parameters_.AccessDoorEnabled && _Y > Parameters_.DoorBottom && _Y < _DoorTop)
            {
                AddPart(Product_, "main-horizontal-left", _HorizontalStart, _Y,
                    Parameters_.DoorLeft, _Y, Parameters_.Horizontal);
                AddPart(Product_, "main-horizontal-right", _DoorRight, _Y,
                    _HorizontalEnd, _Y, Parameters_.Horizontal);
            }
            else
            {
                AddPart(Product_, "main-horizontal", _HorizontalStart, _Y,
                    _HorizontalEnd, _Y, Parameters_.Horizontal);
            }
        }

        const auto _VerticalLeft = _HasSideFrame
            ? Parameters_.Frame.Width : Parameters_.Frame.Width / 2.0;
        const auto _VerticalRight = _HasSideFrame
            ? Parameters_.Width - Parameters_.Frame.Width
            : Parameters_.Width - Parameters_.Frame.Width / 2.0;
        for (const auto _X : EvenPositions(
            Parameters_.MiddleVerticalCount, _VerticalLeft, _VerticalRight))
        {
            if (Parameters_.AccessDoorEnabled && _X > Parameters_.DoorLeft && _X < _DoorRight)
            {
                AddPart(Product_, "main-vertical-bottom", _X, _VerticalStart,
                    _X, Parameters_.DoorBottom, Parameters_.Vertical);
                AddPart(Product_, "main-vertical-top", _X, _DoorTop,
                    _X, _VerticalEnd, Parameters_.Vertical);
            }
            else
            {
                AddPart(Product_, "main-vertical", _X, _VerticalStart,
                    _X, _VerticalEnd, Parameters_.Vertical);
            }
        }
    }

    struct SDoorPartIndices final
    {
        std::uint64_t FixedLeft = 0;
        std::uint64_t FixedRight = 0;
        std::uint64_t LeafLeft = 0;
        std::uint64_t LeafRight = 0;
    };

    SDoorPartIndices AddAccessDoor(
        SGeneratedProduct& Product_, const SSingleFaceSecurityWindowParameters& Parameters_)
    {
        const auto _DoorRight = Parameters_.DoorLeft + Parameters_.DoorWidth;
        const auto _DoorTop = Parameters_.DoorBottom + Parameters_.DoorHeight;
        SDoorPartIndices _Indices;
        const auto _FixedFrame = AddRectangularTubeAssembly(
            Product_, Parameters_.DoorFrameProcess,
            "access-door-fixed-",
            "access-door-fixed-frame", "access-door-fixed-frame", "固定门框",
            Parameters_.DoorLeft, Parameters_.DoorBottom,
            _DoorRight, _DoorTop, Parameters_.DoorFrame);
        _Indices.FixedLeft = _FixedFrame.Left;
        _Indices.FixedRight = _FixedFrame.Right;

        const auto _LeafInset = Parameters_.DoorFrame.Width / 2.0
            + Parameters_.DoorGap + Parameters_.DoorLeafFrame.Width / 2.0;
        const auto _LeafLeft = Parameters_.DoorLeft + _LeafInset;
        const auto _LeafRight = _DoorRight - _LeafInset;
        const auto _LeafBottom = Parameters_.DoorBottom + _LeafInset;
        const auto _LeafTop = _DoorTop - _LeafInset;
        const auto _LeafFrame = AddRectangularTubeAssembly(
            Product_, Parameters_.DoorLeafFrameProcess,
            "access-door-leaf-",
            "access-door-leaf", "access-door-leaf", "活动门扇",
            _LeafLeft, _LeafBottom, _LeafRight, _LeafTop, Parameters_.DoorLeafFrame);
        _Indices.LeafLeft = _LeafFrame.Left;
        _Indices.LeafRight = _LeafFrame.Right;

        const auto _InnerLeft = _LeafLeft + Parameters_.DoorLeafFrame.Width / 2.0;
        const auto _InnerRight = _LeafRight - Parameters_.DoorLeafFrame.Width / 2.0;
        const auto _InnerBottom = _LeafBottom + Parameters_.DoorLeafFrame.Width / 2.0;
        const auto _InnerTop = _LeafTop - Parameters_.DoorLeafFrame.Width / 2.0;
        for (const auto _Y : EvenPositions(Parameters_.DoorHorizontalCount, _InnerBottom, _InnerTop))
            AddPart(Product_, "access-door-leaf-horizontal", _InnerLeft, _Y,
                _InnerRight, _Y, Parameters_.DoorHorizontal);
        for (const auto _X : EvenPositions(Parameters_.DoorVerticalCount, _InnerLeft, _InnerRight))
            AddPart(Product_, "access-door-leaf-vertical", _X, _InnerBottom,
                _X, _InnerTop, Parameters_.DoorVertical);
        AssignPreviewAssembly(Product_, _LeafFrame.First,
            "access-door-leaf", "access-door-leaf", "活动门扇");
        return _Indices;
    }

    void AddCrossingJoints(
        SGeneratedProduct& Product_, double Clearance_, bool ContinuousOuterFrame_)
    {
        for (const auto& _Vertical : Product_.Parts)
        {
            if (!_Vertical.IsVertical()) continue;
            const auto _X = (_Vertical.X1 + _Vertical.X2) / 2.0;
            for (const auto& _Horizontal : Product_.Parts)
            {
                if (_Horizontal.IsVertical() || _Horizontal.Index == _Vertical.Index) continue;
                if (IsFrameRole(_Vertical.Role) && IsFrameRole(_Horizontal.Role)) continue;
                if (ContinuousOuterFrame_ && _Horizontal.Role == "outer-frame") continue;
                if (IsDoorLeafRole(_Vertical.Role) != IsDoorLeafRole(_Horizontal.Role)
                    && (IsDoorLeafRole(_Vertical.Role) || IsDoorLeafRole(_Horizontal.Role))) continue;
                const auto _Y = (_Horizontal.Y1 + _Horizontal.Y2) / 2.0;
                if (_X < std::min(_Horizontal.X1, _Horizontal.X2) - kGeometryTolerance
                    || _X > std::max(_Horizontal.X1, _Horizontal.X2) + kGeometryTolerance
                    || _Y < std::min(_Vertical.Y1, _Vertical.Y2) - kGeometryTolerance
                    || _Y > std::max(_Vertical.Y1, _Vertical.Y2) + kGeometryTolerance) continue;
                const auto _HorizontalIsTarget = ProfileSize(_Horizontal) >= ProfileSize(_Vertical);
                Product_.Joints.push_back({
                    _HorizontalIsTarget ? _Horizontal.Index : _Vertical.Index,
                    _HorizontalIsTarget ? _Vertical.Index : _Horizontal.Index,
                    "through", Clearance_, _X, _Y, {}
                });
            }
        }
        if (!ContinuousOuterFrame_) return;
        for (const auto& _Part : Product_.Parts)
        {
            if (_Part.Role.starts_with("main-horizontal") || _Part.Role.starts_with("main-vertical"))
            {
                Product_.Joints.push_back({
                    1, _Part.Index, "through", Clearance_,
                    (_Part.X1 + _Part.X2) / 2.0, (_Part.Y1 + _Part.Y2) / 2.0, {}
                });
            }
        }
    }

    void AddHingeJoints(
        SGeneratedProduct& Product_, const SSingleFaceSecurityWindowParameters& Parameters_,
        const SDoorPartIndices& Door_)
    {
        const auto _Fixed = Parameters_.DoorHingeSide == "left" ? Door_.FixedLeft : Door_.FixedRight;
        const auto _Leaf = Parameters_.DoorHingeSide == "left" ? Door_.LeafLeft : Door_.LeafRight;
        const auto _X = Parameters_.DoorHingeSide == "left"
            ? Parameters_.DoorLeft : Parameters_.DoorLeft + Parameters_.DoorWidth;
        for (std::uint64_t _Index = 0; _Index < Parameters_.DoorHingeCount; ++_Index)
        {
            const auto _Y = Parameters_.DoorBottom + Parameters_.DoorHeight
                * static_cast<double>(_Index + 1)
                / static_cast<double>(Parameters_.DoorHingeCount + 1);
            Product_.Joints.push_back({
                _Fixed, _Leaf, "hinge", 0.0, _X, _Y,
                "access-door/hinge/" + std::to_string(_Index + 1)
            });
        }
    }

    void Validate(const SSingleFaceSecurityWindowParameters& Parameters_)
    {
        if (Parameters_.ProductCode.empty()) throw std::invalid_argument("product code is required");
        if (!std::isfinite(Parameters_.Height) || !std::isfinite(Parameters_.Width)
            || Parameters_.Height <= 0.0 || Parameters_.Width <= 0.0)
            throw std::invalid_argument("product width and height must be positive");
        if (Parameters_.HorizontalCount > 100 || Parameters_.MiddleVerticalCount > 100
            || Parameters_.DoorHorizontalCount > 100 || Parameters_.DoorVerticalCount > 100
            || Parameters_.DoorHingeCount > 10)
            throw std::invalid_argument("bar or hinge count exceeds the supported range");
        if (!std::isfinite(Parameters_.FirstHorizontalTopOffset)
            || !std::isfinite(Parameters_.LastHorizontalBottomOffset)
            || Parameters_.FirstHorizontalTopOffset < 0.0
            || Parameters_.LastHorizontalBottomOffset < 0.0
            || Parameters_.FirstHorizontalTopOffset > Parameters_.Height
            || Parameters_.LastHorizontalBottomOffset > Parameters_.Height)
            throw std::invalid_argument("horizontal offsets must be inside the product height");
        if (!std::isfinite(Parameters_.HorizontalBranchReserve)
            || !std::isfinite(Parameters_.VerticalBranchReserve)
            || (Parameters_.HorizontalBranchReserve < 0.0 && Parameters_.HorizontalBranchReserve != -1.0)
            || (Parameters_.VerticalBranchReserve < 0.0 && Parameters_.VerticalBranchReserve != -1.0))
            throw std::invalid_argument("branch reserve must be -1 or non-negative");
        if (!std::isfinite(Parameters_.AssemblyClearance) || Parameters_.AssemblyClearance < 0.0)
            throw std::invalid_argument("assembly clearance cannot be negative");
        ValidateProfile(Parameters_.Frame, "outer frame");
        ValidateProfile(Parameters_.Horizontal, "horizontal bar");
        ValidateProfile(Parameters_.Vertical, "vertical bar");
        ValidateProfile(Parameters_.DoorFrame, "access door fixed frame");
        ValidateProfile(Parameters_.DoorLeafFrame, "access door leaf frame");
        ValidateProfile(Parameters_.DoorHorizontal, "access door horizontal bar");
        ValidateProfile(Parameters_.DoorVertical, "access door vertical bar");
        if (Parameters_.Frame.Type != "rect" || Parameters_.Horizontal.Type != "rect"
            || Parameters_.DoorFrame.Type != "rect" || Parameters_.DoorLeafFrame.Type != "rect"
            || Parameters_.DoorHorizontal.Type != "rect")
            throw std::invalid_argument("frames and horizontal bars must use rectangular tube");
        if (Parameters_.Vertical.Type != "round" || Parameters_.DoorVertical.Type != "round")
            throw std::invalid_argument("vertical bars must use round tube");
        if (Parameters_.Frame.Width <= Parameters_.Horizontal.Width
            || Parameters_.Horizontal.Width <= Parameters_.Vertical.Width)
            throw std::invalid_argument("member sizes must follow frame > horizontal > vertical");
        if (Parameters_.Width <= Parameters_.Frame.Width * 2.0
            || Parameters_.Height <= Parameters_.Frame.Width * 2.0)
            throw std::invalid_argument("product dimensions must exceed twice the frame width");
        const auto _HorizontalReserve = Parameters_.HorizontalBranchReserve < 0.0
            ? Parameters_.Frame.Width / 2.0 : Parameters_.HorizontalBranchReserve;
        const auto _VerticalReserve = Parameters_.VerticalBranchReserve < 0.0
            ? Parameters_.Frame.Width / 2.0 : Parameters_.VerticalBranchReserve;
        if (_HorizontalReserve > Parameters_.Frame.Width || _VerticalReserve > Parameters_.Frame.Width)
            throw std::invalid_argument("branch reserve cannot exceed frame width");
        if (Parameters_.DoorHingeSide != "left" && Parameters_.DoorHingeSide != "right")
            throw std::invalid_argument("access door hinge side is unsupported");
        if (!Parameters_.AccessDoorEnabled) return;
        if (!std::isfinite(Parameters_.DoorLeft) || !std::isfinite(Parameters_.DoorBottom)
            || !std::isfinite(Parameters_.DoorWidth) || !std::isfinite(Parameters_.DoorHeight)
            || !std::isfinite(Parameters_.DoorGap) || Parameters_.DoorWidth <= 0.0
            || Parameters_.DoorHeight <= 0.0 || Parameters_.DoorGap < 0.0)
            throw std::invalid_argument("access door dimensions are invalid");
        const auto _DoorRight = Parameters_.DoorLeft + Parameters_.DoorWidth;
        const auto _DoorTop = Parameters_.DoorBottom + Parameters_.DoorHeight;
        if (Parameters_.DoorLeft <= Parameters_.Frame.Width
            || _DoorRight >= Parameters_.Width - Parameters_.Frame.Width
            || Parameters_.DoorBottom <= Parameters_.Frame.Width
            || _DoorTop >= Parameters_.Height - Parameters_.Frame.Width)
            throw std::invalid_argument("access door must remain inside the security window");
        const auto _RequiredInset = Parameters_.DoorFrame.Width
            + Parameters_.DoorLeafFrame.Width + Parameters_.DoorGap * 2.0;
        if (Parameters_.DoorWidth <= _RequiredInset || Parameters_.DoorHeight <= _RequiredInset)
            throw std::invalid_argument("access door is too small for its frames and gap");
    }
}

iCAX::TubeDesigner::SGeneratedProduct
iCAX::TubeDesigner::GenerateSingleFaceSecurityWindow(
    const SSingleFaceSecurityWindowParameters& Parameters_)
{
    Validate(Parameters_);
    SGeneratedProduct _Result;
    _Result.ProductCode = Parameters_.ProductCode;
    _Result.TemplateID = "single-face-security-window";
    _Result.TemplateVersion = "1.0.0";
    _Result.Width = Parameters_.Width;
    _Result.Height = Parameters_.Height;

    AddOuterFrame(_Result, Parameters_);
    AddMainGrid(_Result, Parameters_);
    SDoorPartIndices _Door;
    if (Parameters_.AccessDoorEnabled) _Door = AddAccessDoor(_Result, Parameters_);
    const auto _ContinuousOuterFrame = Parameters_.FrameLayout == "four_sides"
        && Parameters_.OuterFrameProcess.JoinType == "v_groove_90";
    AddCrossingJoints(_Result, Parameters_.AssemblyClearance, _ContinuousOuterFrame);
    if (Parameters_.AccessDoorEnabled) AddHingeJoints(_Result, Parameters_, _Door);
    return _Result;
}
