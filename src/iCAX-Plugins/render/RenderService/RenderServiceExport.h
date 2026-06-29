#pragma once

#ifdef _RENDER_SERVICE
#define _RENDER_SERVICE_EXP __declspec(dllexport)
#else
#define _RENDER_SERVICE_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
