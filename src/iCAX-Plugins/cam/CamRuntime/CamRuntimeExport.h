#pragma once

#ifdef _CAM_RUNTIME
#define _CAM_RUNTIME_EXP __declspec(dllexport)
#else
#define _CAM_RUNTIME_EXP __declspec(dllimport)
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif
