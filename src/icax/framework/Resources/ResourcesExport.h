#pragma once

#ifdef _RESOURCES
#define _RESOURCES_EXP __declspec(dllexport)
#else
#define _RESOURCES_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
