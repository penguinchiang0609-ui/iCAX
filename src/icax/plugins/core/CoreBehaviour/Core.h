#pragma once

#ifdef _COREBEHAVIOUR
#define _COREBEHAVIOUR_EXP __declspec(dllexport)
#else
#define _COREBEHAVIOUR_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT