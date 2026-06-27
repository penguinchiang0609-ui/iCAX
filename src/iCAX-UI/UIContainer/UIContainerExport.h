#pragma once

#ifdef _UI_CONTAINER
#define _UI_CONTAINER_EXP __declspec(dllexport)
#else
#define _UI_CONTAINER_EXP __declspec(dllimport)
#endif
