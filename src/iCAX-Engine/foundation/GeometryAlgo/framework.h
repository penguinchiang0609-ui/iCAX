#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef GEOMETRYALGO_EXPORTS
#define GEOMETRYALGO_API __declspec(dllexport)
#else
#define GEOMETRYALGO_API __declspec(dllimport)
#endif
