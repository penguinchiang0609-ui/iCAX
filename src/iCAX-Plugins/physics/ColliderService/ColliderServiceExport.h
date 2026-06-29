#pragma once

#ifdef _COLLIDER_SERVICE
#define _COLLIDER_SERVICE_EXP __declspec(dllexport)
#else
#define _COLLIDER_SERVICE_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
