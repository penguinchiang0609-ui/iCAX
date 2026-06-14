#pragma once

#ifdef _SAMPLECOMPONENT
#define _SAMPLECOMPONENT_EXP __declspec(dllexport)
#else
#define _SAMPLECOMPONENT_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT