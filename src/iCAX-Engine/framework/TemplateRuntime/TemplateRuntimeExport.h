#pragma once

#ifdef _TEMPLATE_RUNTIME
#define _TEMPLATE_RUNTIME_EXP __declspec(dllexport)
#else
#define _TEMPLATE_RUNTIME_EXP __declspec(dllimport)
#endif

