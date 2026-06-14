#pragma once

#ifdef _RENDERSERVICE
#define _RENDERSERVICE_EXP __declspec(dllexport)
#else
#define _RENDERSERVICE_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT