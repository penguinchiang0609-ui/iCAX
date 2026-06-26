#pragma once

#ifdef _TASK
#define _TASK_EXP __declspec(dllexport)
#else
#define _TASK_EXP __declspec(dllimport)
#endif

