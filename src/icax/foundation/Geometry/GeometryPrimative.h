#pragma once

#ifdef _GEOMETRY
#define _GEOMETRY_EXP __declspec(dllexport)
#else
#define _GEOMETRY_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT