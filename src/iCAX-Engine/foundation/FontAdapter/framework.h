#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef FONTADAPTER_EXPORTS
#define FONTADAPTER_API __declspec(dllexport)
#else
#define FONTADAPTER_API __declspec(dllimport)
#endif
