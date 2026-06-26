#pragma once

#ifdef _WEB_PAGE_HOST
#define _WEB_PAGE_HOST_EXP __declspec(dllexport)
#else
#define _WEB_PAGE_HOST_EXP __declspec(dllimport)
#endif
