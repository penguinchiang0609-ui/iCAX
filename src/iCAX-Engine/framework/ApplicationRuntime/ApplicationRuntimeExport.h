#pragma once

#ifdef _APPLICATION_RUNTIME
#define _APPLICATION_RUNTIME_EXP __declspec(dllexport)
#else
#define _APPLICATION_RUNTIME_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
