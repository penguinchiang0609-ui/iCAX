#pragma once

#ifdef _INPUTSERVICE
#define _INPUTSERVICE_EXP __declspec(dllexport)
#else
#define _INPUTSERVICE_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT