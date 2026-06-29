#pragma once

#ifdef _JOLT_PHYSICS
#define _JOLT_PHYSICS_EXP __declspec(dllexport)
#else
#define _JOLT_PHYSICS_EXP __declspec(dllimport)
#endif
