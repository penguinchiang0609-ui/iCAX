#pragma once

#ifdef _SAMPLESERVICE
#define _SAMPLESERVICE_EXP __declspec(dllexport)
#else
#define _SAMPLESERVICE_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT