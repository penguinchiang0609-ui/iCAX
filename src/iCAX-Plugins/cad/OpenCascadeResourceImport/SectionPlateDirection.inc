// All construction, holes, keep-outs and contact checks share one orthonormal
// support frame. Only final geometry and 3D evidence return to input coordinates.
struct PlateDirectionFrame {
    gp_Trsf ToWorld,ToLocal;
    explicit PlateDirectionFrame(const std::array<double,3>& direction){
        double scale=0;for(auto n:direction){if(!std::isfinite(n))throw std::invalid_argument("PLATE_DIRECTION_INVALID");scale=std::max(scale,std::abs(n));}
        if(scale==0)throw std::invalid_argument("PLATE_DIRECTION_INVALID");
        gp_Vec z(direction[0]/scale,direction[1]/scale,direction[2]/scale);z.Normalize();
        gp_Vec x(1,0,0);x-=z.Multiplied(x.Dot(z));if(x.SquareMagnitude()<1e-12){x=gp_Vec(0,1,0);x-=z.Multiplied(x.Dot(z));}x.Normalize();
        const auto y=z.Crossed(x);
        ToWorld.SetValues(x.X(),y.X(),z.X(),0,x.Y(),y.Y(),z.Y(),0,x.Z(),y.Z(),z.Z(),0);ToLocal=ToWorld.Inverted();
    }
    static gp_Trsf Transform(const std::array<double,16>& m){gp_Trsf t;t.SetValues(m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11]);return t;}
    static std::array<double,16> Matrix(const gp_Trsf& t){std::array<double,16> m{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};for(int i=0;i<3;++i)for(int j=0;j<4;++j)m[i*4+j]=t.Value(i+1,j+1);return m;}
    static std::array<double,3> Point(const gp_Trsf& t,const std::array<double,3>& p){const auto q=gp_Pnt(p[0],p[1],p[2]).Transformed(t);return {q.X(),q.Y(),q.Z()};}
    SectionPlateInput Align(const SectionPlateInput& original)const{
        auto input=original;input.Transform=Matrix(ToLocal*Transform(original.Transform));input.SupportDirection={0,0,1};
        for(auto& keepOut:input.KeepOuts)for(auto& p:keepOut.Vertices)p=Point(ToLocal,p);return input;
    }
    void Restore(SectionPlate& plate)const{
        plate.Frame=Matrix(ToWorld*Transform(plate.Frame));plate.ProjectionFrame=Matrix(ToWorld);
        for(auto& p:plate.Contacts)p=Point(ToWorld,p);plate.CenterOfMass=Point(ToWorld,plate.CenterOfMass);
    }
};
