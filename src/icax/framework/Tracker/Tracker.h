#pragma once

#ifdef _TRACKER
#define _TRACKER_EXP __declspec(dllexport)
#else
#define _TRACKER_EXP __declspec(dllimport)
#endif // _DATABASE

