#pragma once

#ifdef _SAMPLEBEHAVIOUR
#define _SAMPLEBEHAVIOUR_EXP __declspec(dllexport)
#else
#define _SAMPLEBEHAVIOUR_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT