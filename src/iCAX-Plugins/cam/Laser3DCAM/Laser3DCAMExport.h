#pragma once

#ifdef _LASER_3D_CAM
#define _LASER_3D_CAM_EXP __declspec(dllexport)
#else
#define _LASER_3D_CAM_EXP __declspec(dllimport)
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif
