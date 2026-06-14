#pragma once

#ifdef _CORECOMPONENT
#define _CORECOMPONENT_EXP __declspec(dllexport)
#else
#define _CORECOMPONENT_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT