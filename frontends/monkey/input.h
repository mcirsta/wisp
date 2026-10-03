/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_MONKEY_INPUT_H
#define WISP_MONKEY_INPUT_H

#include <stdbool.h>
#include <windows.h>

/* Windows anonymous stdin pipes cannot join a socket wait. This reader hands
 * one complete command at a time to the engine thread; it never runs commands. */
bool monkey_input_start(void);
HANDLE monkey_input_event(void);
char *monkey_input_command(void); /* NULL at EOF; valid until next/finish. */
void monkey_input_next(void);
/* Call after receiving a command/EOF, before requesting another read. */
void monkey_input_finish(void);

#endif
