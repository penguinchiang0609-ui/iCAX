namespace {
    // Motion and joint limits have no dependency on collision geometry. Only
    // keyposes use IK; collision and playback consume these same joint knots.
    struct HeadJointMotion {
        using Pose=iCAX::GeometryServices::RigidMotionPose;
        using Stop=std::function<bool()>;
        MachineSnapshot Machine;
        std::shared_ptr<Collision::IMachineKinematicsService> Kinematics;
        iCAX::GeometryServices::MachineKinematics::ModelRef Model;
        iCAX::GeometryServices::MachineKinematics::Task Task;
        std::vector<Pose> Frames;
        std::vector<std::size_t> Keys;
        std::vector<double> Fractions;
        std::map<std::size_t,std::vector<double>> Solutions;
        std::map<std::size_t,Text> Errors;
        std::map<std::pair<std::size_t,std::size_t>,Text> IntervalErrors;
        std::mutex Mutex;
        std::atomic_size_t Solves=0;
        bool Prepared=false,Valid=false;

        std::vector<double> Place(std::vector<double> q,const Motion::Vector& position)const {
            for(int k=1;k<=3;++k)q[k]=0;
            const auto world=Motion::Forward(Machine.Nodes,q);
            const auto requested=Motion::Transform(world.at(Machine.Target.Support),position);
            const auto tcp=Motion::Transform(world.at(Machine.Target.Tool),Task.TCP);
            for(int k=0;k<3;++k)q[k+1]=requested[k]-tcp[k];
            for(std::size_t k=0;k<q.size();++k)if(!std::isfinite(q[k])||q[k]<Machine.Nodes[k].Lower-1e-9||q[k]>Machine.Nodes[k].Upper+1e-9)
                throw std::runtime_error("CAM_HEAD_LIMIT_NO_SOLUTION");
            return q;
        }
        std::vector<double> At(std::size_t frame)const {
            const auto key=[&](std::size_t index){
                if(Errors.contains(index))throw std::runtime_error(Errors.at(index));
                if(!Solutions.contains(index))throw std::runtime_error("CAM_HEAD_LIMIT_NOT_CHECKED");
                return Solutions.at(index);
            };
            const auto upper=std::lower_bound(Keys.begin(),Keys.end(),frame);
            if(upper==Keys.end())return key(Keys.back());
            if(*upper==frame||upper==Keys.begin())return key(*upper);
            auto a=key(*std::prev(upper));const auto b=key(*upper);
            for(std::size_t k=0;k<a.size();++k)a[k]+=(b[k]-a[k])*Fractions.at(frame);
            return a;
        }
        void Solve(std::size_t frame,unsigned budget,const Stop& cancelled){
            namespace K=iCAX::GeometryServices::MachineKinematics;
            namespace R=iCAX::GeometryServices::RigidMotion;
            try{
                if(cancelled&&cancelled())throw std::runtime_error("MOTION_CANCELLED");
                K::Request request;request.TaskDefinition=Task;
                request.Goal.Direction=R::Unit(R::Rotate(Frames.at(frame).Rotation,{0,0,-1}));
                request.MaxTimeMs=std::min(500u,budget);request.MaxSeeds=8;request.MaxCandidates=1;
                request.PositionToleranceMm=.0001;request.DirectionToleranceRad=1e-7;
                auto seed=MachineJointValues(Machine);const auto initial=Motion::Forward(Machine.Nodes,seed);
                const auto tcp=Motion::Transform(initial.at(Machine.Target.Tool),Task.TCP),origin=Motion::Transform(initial.at(Machine.Target.Support),{0,0,0});
                for(int k=0;k<3;++k)seed.at(1+k)+=origin[k]-tcp[k];request.Seeds={seed};++Solves;
                const auto solved=Kinematics->InverseCandidates(Model,request,cancelled);
                if(cancelled&&cancelled())throw std::runtime_error("MOTION_CANCELLED");
                if(solved.Candidates.empty())throw std::runtime_error(solved.State==K::Status::BudgetExhausted?"CAM_HEAD_IK_BUDGET":solved.State==K::Status::NoCandidate?"CAM_HEAD_LIMIT_NO_SOLUTION":"CAM_HEAD_JOINT_POSE_UNRESOLVED");
                auto q=solved.Candidates.front().Positions;Place(q,Frames.at(frame).Position);
                std::lock_guard lock(Mutex);Solutions.emplace(frame,std::move(q));
            }catch(const std::exception& error){
                if(cancelled&&cancelled())throw;
                std::lock_guard lock(Mutex);Errors[frame]=error.what();
            }
        }
        void CheckInterval(std::size_t first,std::size_t last,unsigned budget,const Stop& cancelled)const {
            namespace R=iCAX::GeometryServices::RigidMotion;
            auto a=At(first),b=At(last);for(int k=1;k<=3;++k)a[k]=b[k]=0;
            // Rotary coordinates interpolate linearly within their closed limit
            // intervals. Repositioned XYZ follows the original TCP polyline;
            // certify its travel bound rather than treating display samples as IK.
            std::vector<double> knots(last-first+1);double length=0,radius=0;
            for(auto i=first;i<=last;++i){radius=std::max(radius,R::Length(Frames[i].Position));if(i>first){length+=R::Length(R::Sub(Frames[i].Position,Frames[i-1].Position));knots[i-first]=length;}}
            for(std::size_t i=1;i<knots.size();++i)knots[i]=length>1e-12?knots[i]/length:double(i)/(knots.size()-1);
            const auto raw=[&](double t){auto q=a;for(std::size_t k=0;k<q.size();++k)q[k]+=(b[k]-a[k])*t;return q;};
            const auto placed=[&](double t){
                if(first==last)return Place(a,Frames[first].Position);
                const auto upper=std::upper_bound(knots.begin(),knots.end(),t);
                const auto i=std::min<std::size_t>(std::max<std::size_t>(1,upper-knots.begin())-1,knots.size()-2);
                const auto f=knots[i+1]>knots[i]?(t-knots[i])/(knots[i+1]-knots[i]):0.;
                return Place(raw(t),R::Add(Frames[first+i].Position,R::Scale(R::Sub(Frames[first+i+1].Position,Frames[first+i].Position),f)));
            };
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(budget);
            const auto certify=[&](auto&& self,double lo,double hi,unsigned depth)->void{
                if(cancelled&&cancelled())throw std::runtime_error("MOTION_CANCELLED");
                if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("CAM_HEAD_LIMIT_BUDGET");
                const auto q0=placed(lo),q1=placed(hi),r0=raw(lo),r1=raw(hi);
                double margin=1e100;for(int k=1;k<=3;++k)margin=std::min({margin,q0[k]-Machine.Nodes[k].Lower,Machine.Nodes[k].Upper-q0[k],q1[k]-Machine.Nodes[k].Lower,Machine.Nodes[k].Upper-q1[k]});
                const auto travel=length*(hi-lo)+Motion::SweepBound(Machine.Nodes,Machine.Target.Tool,Motion::Length(Task.TCP),r0,r1)
                    +Motion::SweepBound(Machine.Nodes,Machine.Target.Support,radius,r0,r1);
                if(margin+1e-9>=travel)return;
                if(depth>=24)throw std::runtime_error("CAM_HEAD_LIMIT_UNVERIFIED");
                const auto mid=(lo+hi)/2;self(self,lo,mid,depth+1);self(self,mid,hi,depth+1);
            };
            certify(certify,0,first==last?0:1,0);
        }
        bool Prepare(const std::vector<std::size_t>& keys,const std::vector<KeyposeCollision::Interval>& intervals,unsigned workers,unsigned budget,const Stop& cancelled){
            if(Prepared)return Valid;
            KeyposeCollision::ParallelKeyposes(keys.size(),workers,cancelled,[&](std::size_t i){Solve(keys[i],budget,cancelled);});
            Valid=Errors.empty();
            for(const auto& interval:intervals){
                try{CheckInterval(interval.First,interval.Last,budget,cancelled);}
                catch(const std::exception& error){if(cancelled&&cancelled())throw;IntervalErrors[{interval.First,interval.Last}]=error.what();Valid=false;}
            }
            Prepared=true;return Valid;
        }
    };
}
