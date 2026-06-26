#pragma once

#ifdef _APPLICATION
#define _APPLICATION_EXP __declspec(dllexport)
#else
#define _APPLICATION_EXP __declspec(dllimport)
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif
