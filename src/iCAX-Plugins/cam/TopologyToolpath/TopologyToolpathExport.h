#pragma once

#ifdef _TOPOLOGY_TOOLPATH
#define _TOPOLOGY_TOOLPATH_EXP __declspec(dllexport)
#else
#define _TOPOLOGY_TOOLPATH_EXP __declspec(dllimport)
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif
