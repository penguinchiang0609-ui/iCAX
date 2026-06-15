#pragma once

#ifdef _APPLICATION_HOST
#define _APPLICATION_HOST_EXP __declspec(dllexport)
#else
#define _APPLICATION_HOST_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
