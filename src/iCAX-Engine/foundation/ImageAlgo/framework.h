#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef IMAGEALGO_EXPORTS
#define IMAGEALGO_API __declspec(dllexport)
#else
#define IMAGEALGO_API __declspec(dllimport)
#endif
