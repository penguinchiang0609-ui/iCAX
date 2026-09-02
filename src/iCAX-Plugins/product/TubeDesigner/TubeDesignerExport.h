#pragma once

#ifdef _TUBE_DESIGNER
#define _TUBE_DESIGNER_EXP __declspec(dllexport)
#else
#define _TUBE_DESIGNER_EXP __declspec(dllimport)
#endif
