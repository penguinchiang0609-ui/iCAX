#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef IMAGEADAPTER_EXPORTS
#define IMAGEADAPTER_API __declspec(dllexport)
#else
#define IMAGEADAPTER_API __declspec(dllimport)
#endif
