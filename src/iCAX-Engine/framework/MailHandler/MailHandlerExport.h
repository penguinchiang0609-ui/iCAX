#pragma once

#ifdef _MAIL_HANDLER
#define _MAIL_HANDLER_EXP __declspec(dllexport)
#else
#define _MAIL_HANDLER_EXP __declspec(dllimport)
#endif

