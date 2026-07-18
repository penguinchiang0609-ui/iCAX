#pragma once
#ifdef _PROCESS_PATH
#define _PROCESS_PATH_EXP __declspec(dllexport)
#else
#define _PROCESS_PATH_EXP __declspec(dllimport)
#endif
