#include "pch.h"
#include "TubeSideSketchCadService.h"
#include "TubeSideSketchCurveService.h"
#include <etp/Analyzer.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_List.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace iCAX::TubeDesigner
{
    namespace
    {
        using iCAX::Data::ObjectMap;
        using iCAX::Data::VariantArray;
        double Number(const ObjectMap& Map_, const char* Key_, double Default_)
        {
            const auto _It=Map_.find(Key_);
            if (_It==Map_.end()) return Default_;
            const auto& _Value=_It->second;
            double _Number;
            if (_Value.Is<double>()) _Number=_Value.To<double>();
            else if (_Value.Is<int>()) _Number=_Value.To<int>();
            else if (_Value.Is<long long>()) _Number=static_cast<double>(_Value.To<long long>());
            else if (_Value.Is<unsigned long long>()) _Number=static_cast<double>(_Value.To<unsigned long long>());
            else throw std::invalid_argument("端部裁切参数必须是数值");
            if (!std::isfinite(_Number)) throw std::invalid_argument("端部裁切参数必须是有限数值");
            return _Number;
        }
        double Volume(const TopoDS_Shape& Shape_)
        {
            GProp_GProps _Properties;
            BRepGProp::VolumeProperties(Shape_,_Properties);
            return std::abs(_Properties.Mass());
        }
    }

    std::vector<TopoDS_Shape> TubeSideSketchSolids(const TopoDS_Shape& Shape_)
    {
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> _Map;
        TopExp::MapShapes(Shape_,TopAbs_SOLID,_Map);
        std::vector<TopoDS_Shape> _Solids;
        for (int _Index=1;_Index<=_Map.Extent();++_Index)
            if (Volume(_Map(_Index))>1.0e-8) _Solids.push_back(_Map(_Index));
        const auto _Bounds=[](const TopoDS_Shape& Shape) {
            Bnd_Box _Box; BRepBndLib::AddOptimal(Shape,_Box,false,false);
            double _X0,_Y0,_Z0,_X1,_Y1,_Z1; _Box.Get(_X0,_Y0,_Z0,_X1,_Y1,_Z1);
            return std::array<double,6>{_X0,_Y0,_Z0,_X1,_Y1,_Z1};
        };
        std::sort(_Solids.begin(),_Solids.end(),[&](const auto& A,const auto& B){return _Bounds(A)<_Bounds(B);});
        return _Solids;
    }

    STubeSideSketchResult ApplyTubeSideSketchCad(const TopoDS_Shape& Blank_,
        const ObjectMap& Sketch_,const STubeSideSketchOptions& Options_)
    {
        STubeSideSketchResult _Result;
        try
        {
            const auto _Entities=Sketch_.find("entities");
            if (_Entities==Sketch_.end() || !_Entities->second.Is<VariantArray>())
                throw std::invalid_argument("二维草图缺少图形数组");
            if (!_Entities->second.To<VariantArray>().empty())
            {
                _Result=ApplyTubeSideSketchCurves(Blank_,Sketch_,Options_);
                if (!_Result.bOK) return _Result;
            }
            else
            {
                _Result.bOK=true; _Result.Approximate=false; _Result.Shape=Blank_;
                _Result.Method="curve-preserving-profile-surface";
            }
            const auto _CutsIt=Sketch_.find("endCuts");
            if (_CutsIt==Sketch_.end()) return _Result;
            if (!_CutsIt->second.Is<ObjectMap>()) throw std::invalid_argument("端部裁切定义无效");
            const auto _Cuts=_CutsIt->second.To<ObjectMap>();
            etp::AnalyzerOptions _Options;
            _Options.MaterializeFaceSplits=false;
            _Options.DistanceTolerance=Options_.DistanceTolerance;
            _Options.AngularToleranceRadians=Options_.AngularToleranceRadians;
            const auto _Tube=etp::ExtrusionToolpathAnalyzer(_Options).Analyze(Blank_);
            if (_Tube.Shape.IsNull() || _Tube.SectionWires.empty())
                throw std::invalid_argument("无法确定端部裁切的管坯坐标系");
            Bnd_Box _Box; BRepBndLib::AddOptimal(_Tube.Shape,_Box,false,false);
            double _X0,_Y0,_Z0,_X1,_Y1,_Z1; _Box.Get(_X0,_Y0,_Z0,_X1,_Y1,_Z1);
            const double _Length=_Tube.AxialLast-_Tube.AxialFirst;
            const gp_Vec _CenterVector(_Tube.SectionFrame.Location(),gp_Pnt((_X0+_X1)/2,(_Y0+_Y1)/2,(_Z0+_Z1)/2));
            const auto _Axis=gp_Vec(_Tube.SectionFrame.Direction());
            const auto _SectionX=gp_Vec(_Tube.SectionFrame.XDirection());
            const auto _SectionY=gp_Vec(_Tube.SectionFrame.YDirection());
            const auto _CenterX=_CenterVector.Dot(_SectionX),_CenterY=_CenterVector.Dot(_SectionY);
            auto _Shape=BRepBuilderAPI_Transform(_Result.Shape,_Tube.SourceToNormalized,false).Shape();
            bool _Trimmed=false;
            std::array<double,2> _Positions{0,_Length};
            for (std::size_t _Index=0;_Index<2;++_Index)
            {
                const auto _It=_Cuts.find(_Index==0?"start":"end");
                if (_It==_Cuts.end()) continue;
                if (!_It->second.Is<ObjectMap>()) throw std::invalid_argument("端部裁切平面无效");
                const auto _Cut=_It->second.To<ObjectMap>();
                const auto _Position=Number(_Cut,"position",_Index==0?0:_Length);
                const auto _Angle=Number(_Cut,"angleDegrees",0);
                const auto _Rotation=Number(_Cut,"rotationDegrees",0);
                if (_Position<0 || _Position>_Length || std::abs(_Angle)>=85 || std::abs(_Rotation)>1.0e6)
                    throw std::invalid_argument("端部位置须在原管长内，斜切角须小于85度");
                _Positions[_Index]=_Position;
                if (std::abs(_Position-(_Index==0?0:_Length))<1.0e-8 && std::abs(_Angle)<1.0e-8) continue;
                const auto _Slope=std::tan(_Angle*std::acos(-1.0)/180);
                const auto _Direction=_Rotation*std::acos(-1.0)/180;
                const gp_Dir _Normal(_Axis-_SectionX*(_Slope*std::cos(_Direction))-_SectionY*(_Slope*std::sin(_Direction)));
                const auto _Origin=_Tube.SectionFrame.Location().Translated(_Axis*(_Tube.AxialFirst+_Position)
                    +_SectionX*_CenterX+_SectionY*_CenterY);
                const gp_Pln _Plane(_Origin,_Normal);
                const auto _Face=BRepBuilderAPI_MakeFace(_Plane).Face();
                const auto _Reference=_Origin.Translated(gp_Vec(_Normal)*(_Index==0?1.0:-1.0));
                const auto _HalfSpace=BRepPrimAPI_MakeHalfSpace(_Face,_Reference).Solid();
                NCollection_List<TopoDS_Shape> _Arguments,_Tools;
                _Arguments.Append(_Shape);_Tools.Append(_HalfSpace);
                BRepAlgoAPI_Common _Common;
                _Common.SetArguments(_Arguments);_Common.SetTools(_Tools);
                _Common.SetNonDestructive(true); _Common.SetFuzzyValue(1.0e-7); _Common.Build();
                if (!_Common.IsDone() || _Common.Shape().IsNull() || !BRepCheck_Analyzer(_Common.Shape()).IsValid()
                    || TubeSideSketchSolids(_Common.Shape()).empty())
                    throw std::invalid_argument("端部裁切后没有有效的管材实体，请调整端部位置和角度");
                _Shape=_Common.Shape(); _Trimmed=true;
            }
            if (_Positions[0]>=_Positions[1]-1.0e-7)
                throw std::invalid_argument("首端位置必须小于尾端位置");
            if (_Trimmed)
            {
                _Result.Shape=BRepBuilderAPI_Transform(_Shape,_Tube.NormalizedToSource,false).Shape();
                if (!BRepCheck_Analyzer(_Result.Shape).IsValid()) throw std::runtime_error("端部裁切实体校验失败");
                _Result.Changed=std::abs(Volume(Blank_)-Volume(_Result.Shape))>1.0e-8;
                _Result.Method+="+end-planes";
                _Result.Diagnostic="已按完整端部平面裁切真实实体";
            }
            _Result.bOK=true;
        }
        catch (const Standard_Failure& Error_) {_Result.bOK=false;_Result.Diagnostic=Error_.what();}
        catch (const std::exception& Error_) {_Result.bOK=false;_Result.Diagnostic=Error_.what();}
        return _Result;
    }
}
