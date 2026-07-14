#pragma once

#ifdef _COMMAND_TARGETS
#define _COMMAND_TARGETS_EXP __declspec(dllexport)
#else
#define _COMMAND_TARGETS_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
