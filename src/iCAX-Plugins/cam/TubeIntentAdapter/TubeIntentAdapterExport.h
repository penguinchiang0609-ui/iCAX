#pragma once
#ifdef _TUBE_INTENT_ADAPTER
#define _TUBE_INTENT_ADAPTER_EXP __declspec(dllexport)
#else
#define _TUBE_INTENT_ADAPTER_EXP __declspec(dllimport)
#endif
