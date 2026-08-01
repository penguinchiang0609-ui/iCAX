#pragma once

#ifdef _ENTITY_VIEW_RUNTIME
#define _ENTITY_VIEW_RUNTIME_EXP __declspec(dllexport)
#else
#define _ENTITY_VIEW_RUNTIME_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
