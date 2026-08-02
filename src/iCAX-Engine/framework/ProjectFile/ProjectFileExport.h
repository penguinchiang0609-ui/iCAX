#pragma once

#ifdef _PROJECT_FILE
#define _PROJECT_FILE_EXP __declspec(dllexport)
#else
#define _PROJECT_FILE_EXP __declspec(dllimport)
#endif

#define IN
#define OUT

