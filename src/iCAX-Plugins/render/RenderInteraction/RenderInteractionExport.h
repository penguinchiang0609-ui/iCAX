#pragma once

#ifdef _RENDER_INTERACTION
#define _RENDER_INTERACTION_EXP __declspec(dllexport)
#else
#define _RENDER_INTERACTION_EXP __declspec(dllimport)
#endif

