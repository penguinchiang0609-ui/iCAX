#pragma once

#ifdef _WINDOWSERVICE
#define _WINDOWSERVICE_EXP __declspec(dllexport)
#else
#define _WINDOWSERVICE_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT