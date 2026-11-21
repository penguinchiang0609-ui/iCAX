#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace iCAX::SheetNesting::Detail
{
    struct LinearRow {std::vector<double> A;double B=0;};
    enum class LinearState { Feasible, Infeasible, Limited };
    // Bounded nonnegative-variable Ax<=b primitive. Feasible points are checked
    // against original rows. Infeasible requires an independently verified
    // Farkas/bound certificate; numerical uncertainty is Limited.
    class LinearFeasibility
    {
        static constexpr double Eps=1e-10;
        std::vector<std::vector<double>> T;
        std::vector<std::size_t> Basic;
        std::size_t Columns=0,RealColumns=0;
        std::size_t Variables=0;
        std::vector<LinearRow> OriginalRows;
        std::vector<long double> Upper;
        bool ValidInput=true;
        std::function<bool()> Stop;
        std::size_t& Work;
        static long double errorBound(std::size_t operations,long double magnitude)
        {
            if(magnitude==0)return 0;
            const long double factor=(operations+4)*std::numeric_limits<long double>::epsilon();
            if(factor>=.01L)return std::numeric_limits<long double>::infinity();
            return factor/(1-factor)*magnitude;
        }
        bool verifyCertificate(const std::vector<long double>& weights)const
        {
            // For Ax<=b, x>=0, a nonnegative y proves infeasibility when
            // b^T y is below min_{0<=x<=upper} (A^T y)^T x. Evaluate this
            // independently in ORIGINAL rows with conservative roundoff bounds.
            long double right=0,rightMagnitude=0;
            for(std::size_t i=0;i<OriginalRows.size();++i)
            {
                if(!std::isfinite(weights[i])||weights[i]<0)return false;
                const long double term=weights[i]*OriginalRows[i].B;
                if(!std::isfinite(term))return false;right+=term;rightMagnitude+=std::abs(term);
            }
            long double lower=0,lowerMagnitude=0;
            for(std::size_t j=0;j<Variables;++j)
            {
                long double coefficient=0,magnitude=0;bool underflow=false;
                for(std::size_t i=0;i<OriginalRows.size();++i)
                {
                    const long double a=OriginalRows[i].A[j],term=weights[i]*a;
                    if(!std::isfinite(term))return false;
                    underflow|=weights[i]!=0&&a!=0&&term==0;
                    coefficient+=term;magnitude+=std::abs(term);
                }
                long double lo=coefficient;
                if(magnitude!=0||underflow)
                {
                    const auto e=errorBound(2*OriginalRows.size(),magnitude)+
                        (underflow?OriginalRows.size()*std::numeric_limits<long double>::denorm_min():0);
                    lo=std::nextafter(coefficient-e,-std::numeric_limits<long double>::infinity());
                }
                if(lo<0)
                {
                    if(!std::isfinite(Upper[j]))return false;
                    const auto term=lo*Upper[j];if(!std::isfinite(term))return false;
                    lower+=term;lowerMagnitude+=std::abs(term);
                }
            }
            if(!std::isfinite(right)||!std::isfinite(lower)||!std::isfinite(rightMagnitude)||!std::isfinite(lowerMagnitude))return false;
            const auto rightHi=std::nextafter(right+errorBound(2*OriginalRows.size(),rightMagnitude),std::numeric_limits<long double>::infinity());
            const auto leftLo=std::nextafter(lower-errorBound(2*Variables,lowerMagnitude),-std::numeric_limits<long double>::infinity());
            const auto guard=1e-12L*(1+std::abs(rightHi)+std::abs(leftLo));
            return rightHi<leftLo-guard;
        }
        bool certifiedInfeasible(const std::vector<double>& reduced)const
        {
            // Original slack reduced costs are -y for the phase-I objective.
            // Tableau signs/values alone are never an infeasibility proof.
            std::vector<long double> weights(OriginalRows.size());long double largest=0;
            for(std::size_t i=0;i<weights.size();++i)
            {
                const auto value=-reduced[Variables+i];if(!std::isfinite(value))return false;
                weights[i]=std::max(0.,value);largest=std::max(largest,weights[i]);
            }
            if(largest==0)return false;for(auto& value:weights)value/=largest;
            if(verifyCertificate(weights))return true;
            // L1 deviation variables have no finite upper bound. Their two
            // defining rows cannot be needed in a position infeasibility ray;
            // remove negative contributions to unbounded columns and REVERIFY.
            bool changed=false;
            for(std::size_t i=0;i<weights.size();++i)if(weights[i]!=0)
                for(std::size_t j=0;j<Variables;++j)if(!std::isfinite(Upper[j])&&OriginalRows[i].A[j]<0)
                {weights[i]=0;changed=true;break;}
            return changed&&verifyCertificate(weights);
        }
        bool verifyPoint(std::vector<double>& full)const
        {
            for(auto& value:full)
            {
                if(!std::isfinite(value)||value< -1e-8)return false;
                if(value<0)value=0;
            }
            for(const auto& row:OriginalRows)
            {
                long double lhs=0,magnitude=std::abs(static_cast<long double>(row.B));
                for(std::size_t j=0;j<Variables;++j)
                {const long double term=static_cast<long double>(row.A[j])*full[j];lhs+=term;magnitude+=std::abs(term);}
                if(!std::isfinite(lhs)||!std::isfinite(magnitude))return false;
                const auto tolerance=1e-12L*(1+magnitude)+errorBound(2*Variables,magnitude);
                if(lhs-static_cast<long double>(row.B)>tolerance)return false;
            }
            return true;
        }
        void pivot(std::size_t r,std::size_t c,std::vector<double>& reduced,double& value)
        {
            const double p=T[r][c];for(auto& x:T[r])x/=p;
            for(std::size_t i=0;i<T.size();++i)if(i!=r)
            {const auto q=T[i][c];if(std::abs(q)>1e-18)for(std::size_t j=0;j<=Columns;++j)T[i][j]-=q*T[r][j];T[i][c]=0;}
            const auto q=reduced[c];value+=q*T[r][Columns];
            for(std::size_t j=0;j<Columns;++j)reduced[j]-=q*T[r][j];reduced[c]=0;Basic[r]=c;++Work;
        }
        LinearState maximise(std::vector<double>& reduced,std::size_t permitted,double& value)
        {
            value=0;
            for(std::size_t i=0;i<T.size();++i)
            {
                const auto q=reduced[Basic[i]];value+=q*T[i][Columns];
                if(q!=0)for(std::size_t j=0;j<Columns;++j)reduced[j]-=q*T[i][j];
            }
            for(std::size_t iteration=0;iteration<20000;++iteration)
            {
                if(Stop&&Stop())return LinearState::Limited;
                if(!std::isfinite(value))return LinearState::Limited;
                for(const auto& row:T)if(!std::isfinite(row[Columns])||row[Columns]< -1e-9)return LinearState::Limited;
                std::size_t enter=Columns;
                // Bland order avoids cycling at exact contacts.
                for(std::size_t j=0;j<permitted;++j)
                {if(!std::isfinite(reduced[j]))return LinearState::Limited;if(reduced[j]>Eps){enter=j;break;}}
                if(enter==Columns)return LinearState::Feasible;
                std::size_t leave=T.size();double ratio=std::numeric_limits<double>::infinity();
                for(std::size_t i=0;i<T.size();++i)if(T[i][enter]>Eps)
                {
                    const double q=std::max(0.,T[i][Columns])/T[i][enter];
                    if(q<ratio-Eps||(std::abs(q-ratio)<=Eps&&(leave==T.size()||Basic[i]<Basic[leave])))
                    {ratio=q;leave=i;}
                }
                // Unboundedness of a numerical auxiliary tableau is not an
                // infeasibility proof of the original position constraints.
                if(leave==T.size())return LinearState::Limited;
                pivot(leave,enter,reduced,value);
            }
            return LinearState::Limited;
        }
    public:
        LinearFeasibility(std::size_t variables,const std::vector<LinearRow>& rows,
            std::size_t& work,std::function<bool()> stop):Variables(variables),OriginalRows(rows),Stop(std::move(stop)),Work(work)
        {
            Upper.assign(Variables,std::numeric_limits<long double>::infinity());
            for(const auto& row:rows)
            {
                if(row.A.size()!=Variables||!std::isfinite(row.B)){ValidInput=false;return;}
                std::size_t nonzero=0,index=0;
                for(std::size_t j=0;j<Variables;++j)
                {if(!std::isfinite(row.A[j])){ValidInput=false;return;}if(row.A[j]!=0){++nonzero;index=j;}}
                if(nonzero==1&&row.A[index]>0)
                {
                    auto upper=static_cast<long double>(row.B)/row.A[index];
                    if(std::isfinite(upper))
                    {
                        upper=std::max(0.L,upper);
                        if(upper!=0)upper=std::nextafter(upper+errorBound(2,std::abs(upper)),std::numeric_limits<long double>::infinity());
                        Upper[index]=std::min(Upper[index],upper);
                    }
                }
            }
            RealColumns=variables+rows.size();Columns=RealColumns;
            for(const auto& row:rows)if(row.B<0)++Columns;
            T.assign(rows.size(),std::vector<double>(Columns+1));Basic.resize(rows.size());
            std::size_t artificial=RealColumns;
            for(std::size_t i=0;i<rows.size();++i)
            {
                const double sign=rows[i].B<0?-1:1;
                for(std::size_t j=0;j<variables;++j)T[i][j]=sign*rows[i].A[j];
                T[i][variables+i]=sign;T[i][Columns]=sign*rows[i].B;
                if(sign<0){T[i][artificial]=1;Basic[i]=artificial++;}else Basic[i]=variables+i;
            }
        }
        LinearState solve(std::size_t positions,std::vector<double>& output,bool feasibilityOnly=false)
        {
            output.clear();
            if(!ValidInput||positions>Variables||(!feasibilityOnly&&positions>Variables/2)||(Stop&&Stop()))return LinearState::Limited;
            std::vector<double> c(Columns);for(std::size_t j=RealColumns;j<Columns;++j)c[j]=-1;
            double value=0;auto state=maximise(c,Columns,value);
            if(state!=LinearState::Feasible)return state;
            double artificialMass=0;
            for(std::size_t i=0;i<T.size();++i)if(Basic[i]>=RealColumns)artificialMass+=std::max(0.,T[i][Columns]);
            if(value< -1e-8||artificialMass>1e-10)
                return certifiedInfeasible(c)?LinearState::Infeasible:LinearState::Limited;
            std::vector<double> dummy(Columns);double zero=0;
            for(std::size_t i=0;i<T.size();)
            {
                if(Stop&&Stop())return LinearState::Limited;
                if(Basic[i]<RealColumns){++i;continue;}
                if(std::abs(T[i][Columns])>1e-10)return LinearState::Limited;
                std::vector<bool> basic(RealColumns,false);for(const auto column:Basic)if(column<RealColumns)basic[column]=true;
                std::size_t c2=RealColumns;double largest=Eps;
                for(std::size_t j=0;j<RealColumns;++j)if(!basic[j]&&std::abs(T[i][j])>largest){largest=std::abs(T[i][j]);c2=j;}
                // A zero artificial basic is degenerate. Largest-magnitude
                // nonbasic pivots avoid dividing phase-I roundoff by the first
                // tiny coefficient in a nearly dependent row.
                T[i][Columns]=0;
                if(c2<RealColumns){pivot(i,c2,dummy,zero);++i;}
                else{T.erase(T.begin()+static_cast<std::ptrdiff_t>(i));Basic.erase(Basic.begin()+static_cast<std::ptrdiff_t>(i));}
            }
            c.assign(Columns,0);if(!feasibilityOnly)for(std::size_t j=positions;j<2*positions;++j)c[j]=-1;
            state=maximise(c,RealColumns,value);if(state!=LinearState::Feasible)return state;
            std::vector<double> full(Variables);
            for(std::size_t i=0;i<T.size();++i)if(Basic[i]<Variables)full[Basic[i]]=T[i][Columns];
            if(!verifyPoint(full))return LinearState::Limited;
            output.assign(full.begin(),full.begin()+static_cast<std::ptrdiff_t>(positions));
            return LinearState::Feasible;
        }
    };
}
