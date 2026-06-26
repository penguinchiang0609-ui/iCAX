#pragma once

#ifdef _COREBEHAVIOUR
#define _COREBEHAVIOUR_EXP __declspec(dllexport)
#else
#define _COREBEHAVIOUR_EXP __declspec(dllimport)
#endif // _COREBEHAVIOUR

#define IN
#define OUT
