#pragma once

#ifdef _COMMAND_HANDLER
#define _COMMAND_HANDLER_EXP __declspec(dllexport)
#else
#define _COMMAND_HANDLER_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
