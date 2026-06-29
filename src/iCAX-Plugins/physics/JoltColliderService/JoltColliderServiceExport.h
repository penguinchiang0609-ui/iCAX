#pragma once

#ifdef _JOLT_COLLIDER_SERVICE
#define _JOLT_COLLIDER_SERVICE_EXP __declspec(dllexport)
#else
#define _JOLT_COLLIDER_SERVICE_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
