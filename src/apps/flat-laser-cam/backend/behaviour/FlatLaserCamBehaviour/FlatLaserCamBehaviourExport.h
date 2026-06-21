#pragma once

#ifdef _FLATLASERCAMBEHAVIOUR
#define _FLATLASERCAMBEHAVIOUR_EXP __declspec(dllexport)
#else
#define _FLATLASERCAMBEHAVIOUR_EXP __declspec(dllimport)
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

