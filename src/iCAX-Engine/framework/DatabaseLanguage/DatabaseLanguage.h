#pragma once

#ifdef _DATABASE_LANGUAGE
    #define _DATABASE_LANGUAGE_EXP __declspec(dllexport)
#else
    #define _DATABASE_LANGUAGE_EXP __declspec(dllimport)
#endif
