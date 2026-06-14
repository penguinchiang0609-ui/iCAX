#pragma once

#ifdef _THREADING
#define _THREADING_EXP __declspec(dllexport)
#else
#define _THREADING_EXP __declspec(dllimport)
#endif // _DATABASE

#define IN
#define OUT