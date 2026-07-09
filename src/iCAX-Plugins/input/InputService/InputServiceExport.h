#pragma once

#ifdef _INPUT_SERVICE
#define _INPUT_SERVICE_EXP __declspec(dllexport)
#else
#define _INPUT_SERVICE_EXP __declspec(dllimport)
#endif

