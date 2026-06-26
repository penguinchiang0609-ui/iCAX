#pragma once

#ifdef _APPLICATION_CONTEXT
#define _APPLICATION_CONTEXT_EXP __declspec(dllexport)
#else
#define _APPLICATION_CONTEXT_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
