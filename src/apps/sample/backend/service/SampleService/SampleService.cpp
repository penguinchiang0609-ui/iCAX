#include "pch.h"
#include "SampleService.h"

int iCAX::Sample::CSampleService::Calc(IN const int& nA_, IN const int& nB_)
{
    if (nB_ == 0)
    {
        throw std::runtime_error("第二个操作数不可以为零");
    }
    return nA_ + nB_;
}
