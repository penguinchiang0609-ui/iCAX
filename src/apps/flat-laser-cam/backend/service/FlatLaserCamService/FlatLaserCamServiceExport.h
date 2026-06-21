#pragma once

#ifdef _FLATLASERCAMSERVICE
#define _FLATLASERCAMSERVICE_EXP __declspec(dllexport)
#else
#define _FLATLASERCAMSERVICE_EXP __declspec(dllimport)
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

