#pragma once

#ifdef _DATABASE
    #define _DATABASE_EXP __declspec(dllexport)
#else
    #define _DATABASE_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT
