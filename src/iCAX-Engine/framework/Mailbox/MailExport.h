#pragma once

#ifdef _MAIL
#define _MAIL_EXP __declspec(dllexport)
#else
#define _MAIL_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
