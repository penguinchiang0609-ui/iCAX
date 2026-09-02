#pragma once

#ifdef _EXTRUSION_RECOGNITION
#define _EXTRUSION_RECOGNITION_EXP __declspec(dllexport)
#else
#define _EXTRUSION_RECOGNITION_EXP __declspec(dllimport)
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

