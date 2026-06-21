#pragma once

#ifdef _FLATLASERCAMCOMPONENT
#define _FLATLASERCAMCOMPONENT_EXP __declspec(dllexport)
#else
#define _FLATLASERCAMCOMPONENT_EXP __declspec(dllimport)
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

