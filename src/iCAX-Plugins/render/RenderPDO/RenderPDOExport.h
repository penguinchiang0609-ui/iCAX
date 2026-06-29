#pragma once

#ifdef _RENDER_PDO
#define _RENDER_PDO_EXP __declspec(dllexport)
#else
#define _RENDER_PDO_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
