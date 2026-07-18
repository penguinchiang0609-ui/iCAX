#pragma once

#ifdef _MACHINE
#define _MACHINE_EXP __declspec(dllexport)
#else
#define _MACHINE_EXP __declspec(dllimport)
#endif
