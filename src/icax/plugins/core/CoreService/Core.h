#pragma once

#ifdef _CORE
#define _CORE_EXP __declspec(dllexport)
#else
#define _CORE_EXP __declspec(dllimport)
#endif // _ASSET
