#pragma once

#ifdef _PDO_RENDER_SERVICE
#define _PDO_RENDER_SERVICE_EXP __declspec(dllexport)
#else
#define _PDO_RENDER_SERVICE_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
