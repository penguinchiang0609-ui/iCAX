#pragma once

#ifdef _INTENT_TOOLPATH
#define _INTENT_TOOLPATH_EXP __declspec(dllexport)
#else
#define _INTENT_TOOLPATH_EXP __declspec(dllimport)
#endif
