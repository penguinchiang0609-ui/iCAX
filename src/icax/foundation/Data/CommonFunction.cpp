#include "pch.h"
#include "CommonFunction.h"

std::size_t HashCombine(std::size_t seed, std::size_t value)
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));

}
