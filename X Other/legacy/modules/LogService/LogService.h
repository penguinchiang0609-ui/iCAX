#pragma once

#ifdef _LOGSERVICE
#define _LOGSERVICE_EXP __declspec(dllexport)
#else
#define _LOGSERVICE_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT