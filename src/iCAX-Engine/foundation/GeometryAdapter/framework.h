#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef GEOMETRYADAPTER_EXPORTS
#define GEOMETRYADAPTER_API __declspec(dllexport)
#else
#define GEOMETRYADAPTER_API __declspec(dllimport)
#endif
