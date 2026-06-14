#pragma once

#ifdef _INTEROP
#define _INTEROP_EXP __declspec(dllexport)
#else
#define _INTEROP_EXP __declspec(dllimport)
#endif // _ASSET
