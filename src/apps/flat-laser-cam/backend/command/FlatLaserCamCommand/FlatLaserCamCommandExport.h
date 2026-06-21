#pragma once

#ifdef _FLATLASERCAMCOMMAND
#define _FLATLASERCAMCOMMAND_EXP __declspec(dllexport)
#else
#define _FLATLASERCAMCOMMAND_EXP __declspec(dllimport)
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

