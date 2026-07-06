#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef FONTALGO_EXPORTS
#define FONTALGO_API __declspec(dllexport)
#else
#define FONTALGO_API __declspec(dllimport)
#endif
