#pragma once

#ifdef _PROJECT
#define _PROJECT_EXP __declspec(dllexport)
#else
#define _PROJECT_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
