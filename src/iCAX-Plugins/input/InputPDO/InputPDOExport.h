#pragma once

#ifdef _INPUT_PDO
#define _INPUT_PDO_EXP __declspec(dllexport)
#else
#define _INPUT_PDO_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
