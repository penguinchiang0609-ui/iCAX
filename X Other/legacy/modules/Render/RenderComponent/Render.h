#pragma once

#ifdef _RENDERCOMPONENT
#define _RENDERCOMPONENT_EXP __declspec(dllexport)
#else
#define _RENDERCOMPONENT_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT