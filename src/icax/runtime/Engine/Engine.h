#pragma once

#ifdef _ENGINE
#define _ENGINE_EXP __declspec(dllexport)
#else
#define _ENGINE_EXP __declspec(dllimport)
#endif // _ASSET
