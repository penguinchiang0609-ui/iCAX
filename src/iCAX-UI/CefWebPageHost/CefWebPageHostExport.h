#pragma once

#ifdef _CEF_WEB_PAGE_HOST
#define _CEF_WEB_PAGE_HOST_EXP __declspec(dllexport)
#else
#define _CEF_WEB_PAGE_HOST_EXP __declspec(dllimport)
#endif
