#pragma once

#ifdef _RENDERBEHAVIOUR
#define _RENDERBEHAVIOUR_EXP __declspec(dllexport)
#else
#define _RENDERBEHAVIOUR_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT