#pragma once

#ifdef _CEF_UI_CONTAINER
#define _CEF_UI_CONTAINER_EXP __declspec(dllexport)
#else
#define _CEF_UI_CONTAINER_EXP __declspec(dllimport)
#endif
