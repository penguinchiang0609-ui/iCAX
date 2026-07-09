#pragma once

#ifdef _CAMERA_NAVIGATION
#define _CAMERA_NAVIGATION_EXP __declspec(dllexport)
#else
#define _CAMERA_NAVIGATION_EXP __declspec(dllimport)
#endif

