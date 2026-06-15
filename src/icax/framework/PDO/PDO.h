#pragma once

#ifdef _PDO
#define _PDO_EXP __declspec(dllexport)
#else
#define _PDO_EXP __declspec(dllimport)
#endif // _PDO
