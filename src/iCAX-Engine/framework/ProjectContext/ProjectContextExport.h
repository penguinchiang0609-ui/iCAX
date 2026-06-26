#pragma once

#ifdef _PROJECT_CONTEXT
#define _PROJECT_CONTEXT_EXP __declspec(dllexport)
#else
#define _PROJECT_CONTEXT_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
