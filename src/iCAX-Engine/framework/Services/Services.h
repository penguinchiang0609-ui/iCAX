#pragma once

#ifdef _SERVICE
#define _SERVICE_EXP __declspec(dllexport)
#else
#define _SERVICE_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT