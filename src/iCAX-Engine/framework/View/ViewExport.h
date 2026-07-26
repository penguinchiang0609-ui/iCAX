#pragma once

#ifdef _VIEW
#define _VIEW_EXP __declspec(dllexport)
#else
#define _VIEW_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
