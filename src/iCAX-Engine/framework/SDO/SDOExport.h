#pragma once

#ifdef ICAX_SDO_EXPORTS
#define _SDO_EXP __declspec(dllexport)
#else
#define _SDO_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
