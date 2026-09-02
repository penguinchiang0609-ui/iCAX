#pragma once

#ifdef _OPEN_CASCADE_RESOURCE_IMPORT
#define _OPEN_CASCADE_RESOURCE_IMPORT_EXP __declspec(dllexport)
#else
#define _OPEN_CASCADE_RESOURCE_IMPORT_EXP __declspec(dllimport)
#endif
