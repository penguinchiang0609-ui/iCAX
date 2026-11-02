// One path, one time law, one delivered polynomial trajectory. COPP is called
// through its C ABI: no STL objects cross the Release SDK / Debug host boundary.
struct SelectedMove {
    std::size_t Edge=0;double First=0,Last=1;
    std::vector<double> A,B;MK::Vector DA{},DB{};std::string Seed,Branch;
};
struct SmoothPiece {SelectedMove Move;MM::AxisSegment Path;double First=0,Last=1;};
double SourceProgress(const SelectedMove& m,const std::vector<double>& q,const MM::Query& query){
    double dot=0,norm=0;for(std::size_t i=0;i<q.size();++i)if(query.MaximumVelocities[i]>0){const auto d=(m.B[i]-m.A[i])/query.MaximumVelocities[i];dot+=(q[i]-m.A[i])/query.MaximumVelocities[i]*d;norm+=d*d;}
    const double value=norm>1e-28?std::clamp(dot/norm,0.,1.):1.;return value<1e-10?0:1-value<1e-10?1:value;
}
MM::Sample TrajectorySample(Context& c,const SelectedMove& m,const std::vector<double>& q){
    MM::Sample sample;sample.SourceStation=m.Edge;sample.SourceFraction=m.First+(m.Last-m.First)*SourceProgress(m,q,c.Q);sample.Positions=q;
    const auto fk=c.Kinematics.Forward(c.Model,q,c.Q.TaskDefinition);sample.TCP=fk.PositionMm;sample.Direction=fk.Direction;sample.DirectionChecked=false;
    if(m.Edge){const auto ref=c.Q.ReferenceAt(m.Edge,sample.SourceFraction,m.DA,m.DB);sample.PositionErrorMm=Distance(fk.PositionMm,ref.Goal.PositionMm);sample.DirectionChecked=ref.CheckDirection;if(ref.CheckDirection)sample.DirectionErrorRad=Angle(fk.Direction,Unit(ref.Goal.Direction));}
    const auto& station=c.Q.Stations[m.Edge];sample.StationID=station.ID;sample.Role=station.Role;sample.SeedSource=m.Seed;sample.BranchKey=m.Branch;return sample;
}
bool CheckCurveReference(Context& c,const SelectedMove& m,const MM::AxisSegment& curve,bool record=false){
    for(std::size_t axis=0;axis<curve.Coefficients.size();++axis){for(const auto coefficient:curve.Coefficients[axis])if(!std::isfinite(coefficient))throw std::runtime_error("MOTION_POLYNOMIAL_NONFINITE");const auto bound=MM::PolynomialBounds(curve.Coefficients[axis]);if(!std::isfinite(bound[0])||!std::isfinite(bound[1]))throw std::runtime_error("MOTION_POLYNOMIAL_BOUND_NONFINITE");if(bound[0]<c.Lower[axis]-1e-8||bound[1]>c.Upper[axis]+1e-8)return false;}
    // Projection onto the certified source chord must advance monotonically.
    MM::Quintic progress{};double norm=0;for(auto axis:c.Active){const double d=(m.B[axis]-m.A[axis])/c.Q.MaximumVelocities[axis];norm+=d*d;for(int k=0;k<6;++k)progress[k]+=curve.Coefficients[axis][k]/c.Q.MaximumVelocities[axis]*d;progress[0]-=m.A[axis]/c.Q.MaximumVelocities[axis]*d;}
    if(norm>1e-28&&MM::PolynomialBounds(progress,0,1,1)[0]<-norm*1e-9)return false;
    const std::size_t probes=std::size_t(1)<<std::min<std::size_t>(c.Q.MinimumVerificationDepth+1,8);
    double positionError=0,directionError=0;for(std::size_t j=0;j<=probes;++j){if(c.Stop())return false;if(++c.R.VerificationSamples>c.Q.MaximumVerificationSamples){c.R.State=MM::Status::BudgetExhausted;c.R.Diagnostic="Continuous-path reference verification budget exhausted.";return false;}const auto sample=TrajectorySample(c,m,MM::Evaluate(curve,double(j)/probes));if(sample.PositionErrorMm>c.Q.PositionToleranceMm||sample.DirectionErrorRad>c.Q.DirectionToleranceRad)return false;positionError=std::max(positionError,sample.PositionErrorMm);directionError=std::max(directionError,sample.DirectionErrorRad);}
    if(record){c.R.MaximumPositionErrorMm=std::max(c.R.MaximumPositionErrorMm,positionError);c.R.MaximumDirectionErrorRad=std::max(c.R.MaximumDirectionErrorRad,directionError);}
    return true;
}
void CoppCheck(CoppStatus status){if(status!=COPP_STATUS_OK)throw std::runtime_error(std::string("MOTION_COPP: ")+copp_status_message(status)+" "+copp_last_error_message());}
struct CoppOwner {
    CoppRobot* Robot=nullptr;CoppVecF64 Seed{},Times{};Copp3SocpResult Result{};
    ~CoppOwner(){copp3_socp_result_free(Result);copp_vec_f64_free(Times);copp_vec_f64_free(Seed);if(Robot)copp_robot_free(Robot);}
};
struct ParameterState {double S=0,V=0,A=0;};
ParameterState ParameterAt(const std::vector<double>& grid,const CoppProfile3rd& p,const CoppVecF64& times,std::size_t i,double u){
    const std::size_t last=grid.size()-1;const double t=times.data[i]+u*(times.data[i+1]-times.data[i]);
    if(i<p.num_stationary_start){const auto end=p.num_stationary_start;const double length=grid[end]-grid.front(),a=p.a.data[end],k=std::pow(a,1.5)/(27*length*length),dt=t-times.data[0];return {grid.front()+k*dt*dt*dt,3*k*dt*dt,6*k*dt};}
    if(i>=last-p.num_stationary_end){const auto begin=last-p.num_stationary_end;const double length=grid.back()-grid[begin],a=p.a.data[begin],k=std::pow(a,1.5)/(27*length*length),dt=t-times.data[last];return {grid.back()+k*dt*dt*dt,3*k*dt*dt,6*k*dt};}
    if(u==0)return {grid[i],std::sqrt(std::max(0.,p.a.data[i])),p.b.data[i]};
    if(u==1)return {grid[i+1],std::sqrt(std::max(0.,p.a.data[i+1])),p.b.data[i+1]};
    const double h=grid[i+1]-grid[i],rate=(p.b.data[i+1]-p.b.data[i])/h;
    const bool reverse=(p.a.data[i+1]==0&&p.a.data[i]!=0)||(p.a.data[i]!=0&&i+1==last-p.num_stationary_end);
    const double dt=reverse?times.data[i+1]-t:t-times.data[i],a=reverse?p.a.data[i+1]:p.a.data[i],b=reverse?-p.b.data[i+1]:p.b.data[i];double x;
    const double z=std::sqrt(std::abs(rate))*dt;
    if(std::abs(z)<1e-6)x=std::sqrt(std::max(0.,a))*dt*(1+rate*dt*dt/6)+b*dt*dt*(.5+rate*dt*dt/24);
    else if(rate>0)x=std::sqrt(a)*std::sinh(z)/std::sqrt(rate)+2*b*std::pow(std::sinh(z/2),2)/rate;
    else x=std::sqrt(a)*std::sin(z)/std::sqrt(-rate)+2*b*std::pow(std::sin(z/2),2)/(-rate);
    x=std::clamp(x,0.,h);const double speed2=a+2*b*x+rate*x*x;return {reverse?grid[i+1]-x:grid[i]+x,std::sqrt(std::max(0.,speed2)),reverse?-(b+rate*x):b+rate*x};
}
struct TrajectoryState {std::vector<double> Q,V,A;};
TrajectoryState PathAt(const SmoothPiece& piece,const ParameterState& parameter){
    const double h=piece.Last-piece.First,u=std::clamp((parameter.S-piece.First)/h,0.,1.);TrajectoryState state;
    for(const auto& axis:piece.Path.Coefficients){const double d=MM::Polynomial(axis,u,1)/h,dd=MM::Polynomial(axis,u,2)/(h*h);state.Q.push_back(MM::Polynomial(axis,u));state.V.push_back(d*parameter.V);state.A.push_back(dd*parameter.V*parameter.V+d*parameter.A);}return state;
}
double DerivativeBound(const MM::Quintic& polynomial,unsigned derivative,double first=0,double last=1,int depth=0){
    const auto range=MM::PolynomialBounds(polynomial,first,last,derivative);const double upper=std::max(std::abs(range[0]),std::abs(range[1]));
    const double lower=std::max({std::abs(MM::Polynomial(polynomial,first,derivative)),std::abs(MM::Polynomial(polynomial,last,derivative)),std::abs(MM::Polynomial(polynomial,(first+last)/2,derivative))});
    if(depth==4||upper<=lower*(1+1e-5)+1e-14)return upper;const double middle=(first+last)/2;return std::max(DerivativeBound(polynomial,derivative,first,middle,depth+1),DerivativeBound(polynomial,derivative,middle,last,depth+1));
}
bool CompileSmoothTrajectoryBlock(Context& c,const std::vector<SelectedMove>& selected){
    std::vector<SmoothPiece> pieces;std::vector<std::pair<std::size_t,SelectedMove>> events;std::vector<double> knots{0};
    for(const auto& move:selected){double width=0;for(auto axis:c.Active)width=std::max(width,std::abs(move.B[axis]-move.A[axis])/c.Q.MaximumVelocities[axis]);if(width<1e-14){events.emplace_back(pieces.size(),move);continue;}SmoothPiece piece;piece.Move=move;piece.First=knots.back();piece.Last=piece.First+width;piece.Path.SourceStation=move.Edge;piece.Path.SourceFirst=move.First;piece.Path.SourceLast=move.Last;pieces.push_back(piece);knots.push_back(piece.Last);}
    if(pieces.empty()){for(const auto& [_,m]:events){auto sample=TrajectorySample(c,m,m.B);sample.TimeSeconds=c.R.DurationSeconds;sample.Velocities=c.R.Samples.back().Velocities;sample.Accelerations=c.R.Samples.back().Accelerations;c.R.Samples.push_back(std::move(sample));}return true;}
    const auto count=pieces.size(),axes=c.Lower.size();std::vector<std::vector<double>> point(count+1),d(count+1,std::vector<double>(axes)),dd=d;point[0]=pieces[0].Move.A;for(std::size_t i=0;i<count;++i)point[i+1]=pieces[i].Move.B;
    std::vector<double> blend(count+1,1);
    for(std::size_t k=0;k<=count;++k)for(auto axis:c.Active){
        if(k==0)d[k][axis]=(point[1][axis]-point[0][axis])/(knots[1]-knots[0]);
        else if(k==count)d[k][axis]=(point[k][axis]-point[k-1][axis])/(knots[k]-knots[k-1]);
        else{const double h0=knots[k]-knots[k-1],h1=knots[k+1]-knots[k],v0=(point[k][axis]-point[k-1][axis])/h0,v1=(point[k+1][axis]-point[k][axis])/h1;d[k][axis]=(v0*h1+v1*h0)/(h0+h1);dd[k][axis]=2*(v1-v0)/(h0+h1);}
    }
    for(std::size_t k=1;k<count;++k){const auto& before=pieces[k-1].Move;const auto& after=pieces[k].Move;const auto& a=c.Q.Stations[before.Edge];const auto& b=c.Q.Stations[after.Edge];double dot=0,n0=0,n1=0;for(auto axis:c.Active){const double v0=(point[k][axis]-point[k-1][axis])/c.Q.MaximumVelocities[axis],v1=(point[k+1][axis]-point[k][axis])/c.Q.MaximumVelocities[axis];dot+=v0*v1;n0+=v0*v0;n1+=v1*v1;}
        if((before.Edge!=after.Edge&&a.StopAfter)||(n0*n1>0&&dot/std::sqrt(n0*n1)<.5))blend[k]=0;
    }
    // Geometry/collision acceptance decides blend extent. An exact corner can
    // always retain its original certified trace with zero tangent/curvature.
    for(int attempt=0;;++attempt){bool accepted=true;std::vector<bool> failed(count+1);
        for(std::size_t k=0;k<count;++k){auto& piece=pieces[k];piece.Path.Coefficients.clear();const double h=knots[k+1]-knots[k];for(std::size_t axis=0;axis<axes;++axis)piece.Path.Coefficients.push_back(MM::Hermite(point[k][axis],d[k][axis]*blend[k],dd[k][axis]*blend[k],point[k+1][axis],d[k+1][axis]*blend[k+1],dd[k+1][axis]*blend[k+1],h));
            if(!CheckCurveReference(c,piece.Move,piece.Path)){if(c.Stop()||c.R.State==MM::Status::BudgetExhausted)return false;accepted=false;failed[k]=failed[k+1]=true;continue;}
            ++c.R.MotionChecks;const auto certificate=c.Q.CheckTrajectory(piece.Path);if(c.Stop())return false;if(certificate.State!=MM::Verdict::Accepted){if(attempt>=9)return c.Validation(certificate,piece.Move.Edge,piece.Move.Edge);accepted=false;failed[k]=failed[k+1]=true;}
        }
        if(accepted)break;if(attempt>=10)return c.Validation({MM::Verdict::Unresolved,"No continuous axis path within the original reference tolerance; no linear-timing fallback."},NoStation,NoStation);for(std::size_t k=0;k<=count;++k)if(failed[k])blend[k]=attempt>=7?0:blend[k]*.5;
    }
    for(std::size_t k=1;k<count;++k)if(blend[k]==0)++c.R.StopKnots;else ++c.R.BlendedKnots;
    // Nondimensionalize both distance and time. Mixed mm/radian coordinates and
    // sub-millimetre paths otherwise make the SOCP third-order rows ill-scaled.
    const double nominalDuration=knots.back();double timeUnit=1e-6;std::vector<double> units(axes,1),origins=point.front();
    for(auto axis:c.Active){double low=point.front()[axis],high=low;for(const auto& q:point){low=std::min(low,q[axis]);high=std::max(high,q[axis]);}const double travel=high-low;units[axis]=travel;timeUnit=std::max({timeUnit,travel/c.Q.MaximumVelocities[axis],std::sqrt(travel/c.Q.MaximumAccelerations[axis]),std::cbrt(travel/c.Q.MaximumJerks[axis])});}
    // Axes that remain still must not create 1e9-sized constraint bounds.
    // Scale against physical capabilities over this block's characteristic time.
    for(auto axis:c.Active){units[axis]=std::max(units[axis],c.Q.MaximumVelocities[axis]*timeUnit);if(std::isfinite(c.Q.MaximumAccelerations[axis]))units[axis]=std::max(units[axis],c.Q.MaximumAccelerations[axis]*timeUnit*timeUnit);if(std::isfinite(c.Q.MaximumJerks[axis]))units[axis]=std::max(units[axis],c.Q.MaximumJerks[axis]*std::pow(timeUnit,3));}
    for(auto& knot:knots)knot/=nominalDuration;for(auto& piece:pieces){piece.First/=nominalDuration;piece.Last/=nominalDuration;}
    if(c.Q.Progress)c.Q.Progress("Timing",0,count);
    // Include every polynomial boundary. Grid refinement changes timing only;
    // final physical bounds are checked on the delivered polynomials below.
    const auto subdivisions=std::max<std::size_t>(4,(32+count-1)/count);
    std::vector<double> grid;std::vector<std::size_t> owners;for(std::size_t k=0;k<count;++k)for(std::size_t j=0;j<subdivisions;++j){grid.push_back(knots[k]+(knots[k+1]-knots[k])*j/subdivisions);owners.push_back(k);}grid.push_back(knots.back());owners.push_back(count-1);
    if(grid.size()>100000){c.R.State=MM::Status::BudgetExhausted;c.R.Diagnostic="COPP station budget exceeded; no partial trajectory published.";return false;}
    CoppOwner copp;CoppCheck(copp_robot_create(c.Active.size(),grid.size(),&copp.Robot));CoppCheck(copp_robot_append_s(copp.Robot,{grid.data(),grid.size()}));
    std::array<std::vector<double>,4> values;for(auto& v:values)v.resize(grid.size()*c.Active.size());std::vector<double> cap(grid.size(),std::pow(timeUnit/nominalDuration,2));
    for(std::size_t k=0;k<grid.size();++k){if(c.Stop())return false;const auto& piece=pieces[owners[k]];const double h=piece.Last-piece.First,u=std::clamp((grid[k]-piece.First)/h,0.,1.);auto physical=MM::Evaluate(piece.Path,u);const auto jac=c.Kinematics.Jacobian(c.Model,physical,c.Q.TaskDefinition);MK::Vector tcpDerivative{};
        for(std::size_t j=0;j<c.Active.size();++j){const auto axis=c.Active[j];for(unsigned derivative=0;derivative<4;++derivative)values[derivative][k*c.Active.size()+j]=(MM::Polynomial(piece.Path.Coefficients[axis],u,derivative)-(derivative==0?origins[axis]:0))/std::pow(h,int(derivative))/units[axis];for(int row=0;row<3;++row)tcpDerivative[row]+=jac.Values[row*axes+axis]*MM::Polynomial(piece.Path.Coefficients[axis],u,1)/h;}
        const double speed=Length(tcpDerivative),feed=c.Q.Stations[piece.Move.Edge].FeedMmPerSecond;if(speed>0)cap[k]=std::min(cap[k],std::pow(.98*feed*timeUnit/speed,2));
    }
    const auto matrix=[&](const std::vector<double>& v){return COPP_MATRIX_VIEW_F64_COLUMN_MAJOR(v.data(),c.Active.size(),grid.size());};
    CoppCheck(copp_robot_set_q_3rd(copp.Robot,0,matrix(values[0]),matrix(values[1]),matrix(values[2]),matrix(values[3])));
    std::vector<double> v,negativeV;for(auto axis:c.Active){v.push_back(.98*c.Q.MaximumVelocities[axis]*timeUnit/units[axis]);negativeV.push_back(-v.back());}
    const auto slice=[](const std::vector<double>& value){return CoppSliceF64{value.data(),value.size()};};
    CoppCheck(copp_add_axial_velocity_limits(copp.Robot,0,grid.size(),slice(v),slice(negativeV)));
    // Omit unbounded axes from the optimizer entirely. Large fabricated caps
    // and IEEE infinities must never enter its finite matrix coefficients.
    for(unsigned order:{2u,3u}){
        std::vector<std::size_t> bounded;for(std::size_t j=0;j<c.Active.size();++j)if(std::isfinite(order==2?c.Q.MaximumAccelerations[c.Active[j]]:c.Q.MaximumJerks[c.Active[j]]))bounded.push_back(j);
        if(bounded.size()==c.Active.size()){
            std::vector<double> upper,lower;for(auto axis:c.Active){upper.push_back(.98*(order==2?c.Q.MaximumAccelerations[axis]*timeUnit*timeUnit:c.Q.MaximumJerks[axis]*std::pow(timeUnit,3))/units[axis]);lower.push_back(-upper.back());}
            if(order==2)CoppCheck(copp_add_axial_acceleration_limits(copp.Robot,0,grid.size(),slice(upper),slice(lower)));
            else CoppCheck(copp_add_axial_jerk_limits(copp.Robot,0,grid.size(),slice(upper),slice(lower)));continue;
        }
        const auto rows=2*bounded.size();if(!rows)continue;std::array<std::vector<double>,5> raw;for(auto& values:raw)values.resize(rows*grid.size());
        for(std::size_t k=0;k<grid.size();++k)for(std::size_t r=0;r<bounded.size();++r){const auto j=bounded[r],axis=c.Active[j],source=k*c.Active.size()+j;const double limit=.98*(order==2?c.Q.MaximumAccelerations[axis]*timeUnit*timeUnit:c.Q.MaximumJerks[axis]*std::pow(timeUnit,3))/units[axis];
            for(int sign:{-1,1}){const auto target=k*rows+2*r+(sign>0?1:0);raw[0][target]=sign*values[order][source];raw[1][target]=sign*(order==2?values[1][source]:3*values[2][source]);if(order==3)raw[2][target]=sign*values[1][source];raw[4][target]=limit;}}
        const auto view=[&](int i){return COPP_MATRIX_VIEW_F64_COLUMN_MAJOR(raw[i].data(),rows,grid.size());};
        if(order==2)CoppCheck(copp_add_raw_constraint_2nd(copp.Robot,0,view(0),view(1),view(4)));
        else CoppCheck(copp_add_raw_constraint_3rd(copp.Robot,0,view(0),view(1),view(2),view(3),view(4)));
    }
    CoppCheck(copp_add_raw_constraint_1st(copp.Robot,0,COPP_MATRIX_VIEW_F64_COLUMN_MAJOR(cap.data(),1,grid.size())));
    Topp2RaOptions seedOptions{};CoppCheck(topp2_ra_default_options(&seedOptions));Topp2Problem seedProblem{copp.Robot,0,grid.size()-1,0,0};CoppCheck(topp2_ra(seedProblem,seedOptions,&copp.Seed));CoppCheck(copp_robot_amax_substitute(copp.Robot,{copp.Seed.data,copp.Seed.len},0));
    CoppClarabelOptions options{};CoppCheck(copp_clarabel_default_options(&options));options.allow_almost_solved=true;options.allow_max_time=options.allow_max_iterations=options.allow_insufficient_progress=false;
    for(int round=0;round<2;++round){if(c.Stop())return false;options.clarabel_settings.time_limit=std::max(.001,std::min(5.,c.Q.MaximumSeconds-c.Elapsed()/1000));Topp3Problem problem{};problem.robot=copp.Robot;problem.a_linearization=round?CoppSliceF64{copp.Result.profile.a.data,copp.Result.profile.a.len}:CoppSliceF64{copp.Seed.data,copp.Seed.len};problem.num_stationary_max_start=problem.num_stationary_max_end=1;problem.a_linearization_floor=1e-10;
        Copp3SocpResult next{};const auto status=topp3_socp_expert(problem,options,&next);if(status!=COPP_STATUS_OK){copp3_socp_result_free(next);CoppCheck(status);}const bool usable=next.has_profile&&(int(next.solver_status)==1||int(next.solver_status)==4)&&std::isfinite(next.r_prim)&&std::isfinite(next.r_dual);
        if(!usable){const auto code=next.solver_status;copp3_socp_result_free(next);if(round&&copp.Result.has_profile)break;c.R.State=MM::Status::Unresolved;c.R.Diagnostic="COPP third-order solve rejected (round "+std::to_string(round)+", status "+std::to_string(int(code))+"); no velocity-only fallback.";return false;}c.R.ApproximateSolver|=int(next.solver_status)==4;copp3_socp_result_free(copp.Result);copp.Result=next;
    }
    if(c.Stop())return false;double duration=0;const auto& profile=copp.Result.profile;CoppCheck(copp_s_to_t_3rd(slice(grid),{profile.a.data,profile.a.len},{profile.b.data,profile.b.len},profile.num_stationary_start,profile.num_stationary_end,0,&duration,&copp.Times));
    if(!std::isfinite(duration)||duration<=0){c.R.State=MM::Status::Unresolved;c.R.Diagnostic="COPP returned a non-finite time law.";return false;}
    std::vector<MM::AxisSegment> trajectory;std::vector<SelectedMove> provenance;
    if(c.Q.Progress)c.Q.Progress("Compiling",0,grid.size());
    for(std::size_t k=0;k+1<grid.size();++k){const auto& piece=pieces[owners[k]];const double interval=(copp.Times.data[k+1]-copp.Times.data[k])*timeUnit;if(!std::isfinite(interval)||interval<=0){c.R.State=MM::Status::Unresolved;c.R.Diagnostic="COPP returned a non-positive interval.";return false;}
        const auto at=[&](double u){auto parameter=ParameterAt(grid,profile,copp.Times,k,u);parameter.V/=timeUnit;parameter.A/=timeUnit*timeUnit;return PathAt(piece,parameter);};
        std::function<bool(double,double,int)> fit=[&](double first,double last,int depth){if(c.Stop())return false;const auto start=at(first),end=at(last);MM::AxisSegment segment;segment.SourceStation=piece.Move.Edge;segment.DurationSeconds=interval*(last-first);segment.TimeSeconds=copp.Times.data[k]*timeUnit+interval*first;segment.SourceFirst=piece.Move.First+(piece.Move.Last-piece.Move.First)*SourceProgress(piece.Move,start.Q,c.Q);segment.SourceLast=piece.Move.First+(piece.Move.Last-piece.Move.First)*SourceProgress(piece.Move,end.Q,c.Q);
            for(std::size_t axis=0;axis<axes;++axis)segment.Coefficients.push_back(MM::Hermite(start.Q[axis],start.V[axis],start.A[axis],end.Q[axis],end.V[axis],end.A[axis],segment.DurationSeconds));bool precise=true;
            for(const double u:{.25,.5,.75}){const auto exact=at(first+(last-first)*u);const auto fitted=MM::Evaluate(segment,u);for(auto axis:c.Active)precise&=std::abs(fitted[axis]-exact.Q[axis])<=c.Q.MaximumVelocities[axis]*1e-7;}
            if(!precise||!CheckCurveReference(c,piece.Move,segment,true)){if(c.Stop()||c.R.State==MM::Status::BudgetExhausted)return false;if(depth>=12)return c.Validation({MM::Verdict::Unresolved,"Time-law polynomial conversion could not preserve the accepted contour."},piece.Move.Edge,piece.Move.Edge);const double middle=(first+last)/2;return fit(first,middle,depth+1)&&fit(middle,last,depth+1);}
            if(c.R.Segments.size()+trajectory.size()>=100000){c.R.State=MM::Status::BudgetExhausted;c.R.Diagnostic="Formal polynomial segment budget exhausted.";return false;}trajectory.push_back(std::move(segment));provenance.push_back(piece.Move);return true;
        };if(!fit(0,1,0))return false;
    }
    // Conservative Bernstein enclosures certify all derivative extrema between
    // samples. Global dilation changes no geometry and reduces V/A/J by k/k²/k³.
    if(c.Q.Progress)c.Q.Progress("CheckingDynamics",0,trajectory.size());
    double scale=1,velocityRatio=0,accelerationRatio=0,jerkRatio=0;for(std::size_t k=0;k<trajectory.size();++k){if(c.Stop())return false;const auto& segment=trajectory[k];for(auto axis:c.Active){velocityRatio=std::max(velocityRatio,DerivativeBound(segment.Coefficients[axis],1)/segment.DurationSeconds/c.Q.MaximumVelocities[axis]);accelerationRatio=std::max(accelerationRatio,DerivativeBound(segment.Coefficients[axis],2)/std::pow(segment.DurationSeconds,2)/c.Q.MaximumAccelerations[axis]);jerkRatio=std::max(jerkRatio,DerivativeBound(segment.Coefficients[axis],3)/std::pow(segment.DurationSeconds,3)/c.Q.MaximumJerks[axis]);}
        for(int j=0;j<=8;++j){const double u=j/8.;const auto q=MM::Evaluate(segment,u),v=MM::Evaluate(segment,u,1);const auto jac=c.Kinematics.Jacobian(c.Model,q,c.Q.TaskDefinition);MK::Vector tcp{};for(int row=0;row<3;++row)for(auto axis:c.Active)tcp[row]+=jac.Values[row*axes+axis]*v[axis];scale=std::max(scale,Length(tcp)/c.Q.Stations[provenance[k].Edge].FeedMmPerSecond);}
    }scale=std::max({scale,velocityRatio,std::sqrt(accelerationRatio),std::cbrt(jerkRatio)})*(1+1e-7);
    if(!std::isfinite(scale)||!std::isfinite(velocityRatio)||!std::isfinite(accelerationRatio)||!std::isfinite(jerkRatio))throw std::runtime_error("MOTION_DYNAMICS_NONFINITE");
    c.R.MaximumVelocityRatio=std::max(c.R.MaximumVelocityRatio,velocityRatio/scale);c.R.MaximumAccelerationRatio=std::max(c.R.MaximumAccelerationRatio,accelerationRatio/(scale*scale));c.R.MaximumJerkRatio=std::max(c.R.MaximumJerkRatio,jerkRatio/std::pow(scale,3));
    // Acceptance depends on the delivered function, never on the optimizer's
    // KKT/AlmostSolved label. Every join must be C2 before global dilation.
    for(std::size_t k=0;k<trajectory.size();++k)for(unsigned derivative=0;derivative<=2;++derivative){const auto start=MM::Evaluate(trajectory[k],0,derivative);const auto previous=k?MM::Evaluate(trajectory[k-1],1,derivative):derivative?std::vector<double>(axes,0):point.front();for(std::size_t axis=0;axis<axes;++axis){const double unit=derivative==0?1:derivative==1?std::max(1.,c.Q.MaximumVelocities[axis]):std::isfinite(c.Q.MaximumAccelerations[axis])?std::max(1.,c.Q.MaximumAccelerations[axis]):std::max({1.,std::abs(start[axis]),std::abs(previous[axis])});if(!std::isfinite(start[axis])||!std::isfinite(previous[axis])||std::abs(start[axis]-previous[axis])>unit*1e-6)return c.Validation({MM::Verdict::Unresolved,"Delivered polynomial trajectory has a discontinuous position/velocity/acceleration join."},trajectory[k].SourceStation,trajectory[k].SourceStation);}}
    double time=c.R.DurationSeconds;const auto segmentOffset=c.R.Segments.size();std::size_t eventIndex=0,previousOwner=0;
    const auto appendEvents=[&](std::size_t owner){while(eventIndex<events.size()&&events[eventIndex].first<=owner){auto sample=TrajectorySample(c,events[eventIndex].second,events[eventIndex].second.B);sample.TimeSeconds=time;sample.Velocities=c.R.Samples.back().Velocities;sample.Accelerations=c.R.Samples.back().Accelerations;c.R.Samples.push_back(std::move(sample));++eventIndex;}};
    appendEvents(0);
    for(std::size_t k=0;k<trajectory.size();++k){if(c.Stop())return false;auto& segment=trajectory[k];segment.DurationSeconds*=scale;segment.TimeSeconds=time;time+=segment.DurationSeconds;
        ++c.R.MotionChecks;const auto certificate=c.Q.CheckTrajectory(segment);if(c.Stop())return false;if(!c.Validation(certificate,segment.SourceStation,segment.SourceStation))return false;
        auto sample=TrajectorySample(c,provenance[k],MM::Evaluate(segment,1));sample.SegmentIndex=segmentOffset+k;sample.TimeSeconds=time;sample.Velocities=MM::Evaluate(segment,1,1);sample.Accelerations=MM::Evaluate(segment,1,2);c.R.MaximumPositionErrorMm=std::max(c.R.MaximumPositionErrorMm,sample.PositionErrorMm);c.R.MaximumDirectionErrorRad=std::max(c.R.MaximumDirectionErrorRad,sample.DirectionErrorRad);c.R.Samples.push_back(std::move(sample));
        while(previousOwner<count&&provenance[k].Edge==pieces[previousOwner].Move.Edge&&provenance[k].Last==pieces[previousOwner].Move.Last){if(k+1<trajectory.size()&&provenance[k+1].Edge==pieces[previousOwner].Move.Edge&&provenance[k+1].First==pieces[previousOwner].Move.First&&provenance[k+1].Last==pieces[previousOwner].Move.Last)break;++previousOwner;appendEvents(previousOwner);break;}
    }
    appendEvents(count);c.R.Segments.insert(c.R.Segments.end(),std::make_move_iterator(trajectory.begin()),std::make_move_iterator(trajectory.end()));c.R.DurationSeconds=time;return true;
}
bool CompileSmoothTrajectory(Context& c,const std::vector<SelectedMove>& selected){
    // A process boundary or a sharp exact corner requires rest. Solve each
    // rest-to-rest block separately: a zero path tangent inside a global SOCP
    // leaves the path speed unconstrained and makes its linearization singular.
    std::size_t begin=0,previous=NoStation;
    for(std::size_t k=0;k<selected.size();++k){
        const auto& move=selected[k];double length=0;for(auto axis:c.Active)length+=std::pow((move.B[axis]-move.A[axis])/c.Q.MaximumVelocities[axis],2);
        if(length<1e-28)continue;
        if(previous!=NoStation){const auto& before=selected[previous];double dot=0,norm=0;for(auto axis:c.Active){const double a=(before.B[axis]-before.A[axis])/c.Q.MaximumVelocities[axis],b=(move.B[axis]-move.A[axis])/c.Q.MaximumVelocities[axis];dot+=a*b;norm+=a*a;}
            bool stop=dot/std::sqrt(norm*length)<.5;for(auto j=previous;j<k;++j)stop|=selected[j].Edge!=selected[j+1].Edge&&c.Q.Stations[selected[j].Edge].StopAfter;
            if(stop){if(!CompileSmoothTrajectoryBlock(c,{selected.begin()+begin,selected.begin()+k}))return false;++c.R.StopKnots;begin=k;}
        }previous=k;
    }
    return CompileSmoothTrajectoryBlock(c,{selected.begin()+begin,selected.end()});
}
