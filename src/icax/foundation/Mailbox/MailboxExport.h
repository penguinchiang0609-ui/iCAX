#pragma once

#ifdef _MAILBOX
#define _MAILBOX_EXP __declspec(dllexport)
#else
#define _MAILBOX_EXP __declspec(dllimport)
#endif

#define IN
#define OUT
