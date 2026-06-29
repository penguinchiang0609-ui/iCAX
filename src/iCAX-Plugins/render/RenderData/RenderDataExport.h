#pragma once

#ifdef _RENDER_DATA
#define _RENDER_DATA_EXP __declspec(dllexport)
#else
#define _RENDER_DATA_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
