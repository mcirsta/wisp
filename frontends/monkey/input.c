/* SPDX-License-Identifier: GPL-2.0-only */
#include <limits.h>
#include "monkey/input.h"

static struct {
    HANDLE stdin_handle, thread, ready, next;
    char line[PATH_MAX];
    bool stop, eof;
} input;

static DWORD WINAPI read_commands(void *unused)
{
    (void)unused;
    for (;;) {
        unsigned length = 0;
        DWORD count;
        while (length < sizeof(input.line) - 1) {
            char ch;
            if (!ReadFile(input.stdin_handle, &ch, 1, &count, NULL) || count == 0)
                break;
            input.line[length++] = ch;
            if (ch == '\n')
                break;
        }
        input.line[length] = '\0';
        input.eof = length == 0;
        SetEvent(input.ready);
        if (input.eof)
            return 0;
        /* The main thread acknowledges only after finishing the command.
         * QUIT therefore joins here, with no blocked read to cancel. */
        if (WaitForSingleObject(input.next, INFINITE) != WAIT_OBJECT_0 || input.stop)
            return 0;
    }
}

bool monkey_input_start(void)
{
    input.stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (input.stdin_handle == NULL || input.stdin_handle == INVALID_HANDLE_VALUE)
        return false;
    input.stop = input.eof = false;
    input.ready = CreateEvent(NULL, FALSE, FALSE, NULL);
    input.next = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (input.ready != NULL && input.next != NULL)
        input.thread = CreateThread(NULL, 0, read_commands, NULL, 0, NULL);
    if (input.thread != NULL)
        return true;
    if (input.ready != NULL)
        CloseHandle(input.ready);
    if (input.next != NULL)
        CloseHandle(input.next);
    return false;
}

HANDLE monkey_input_event(void)
{
    return input.ready;
}

char *monkey_input_command(void)
{
    return input.eof ? NULL : input.line;
}

void monkey_input_next(void)
{
    SetEvent(input.next);
}

void monkey_input_finish(void)
{
    input.stop = true;
    SetEvent(input.next);
    WaitForSingleObject(input.thread, INFINITE);
    CloseHandle(input.thread);
    CloseHandle(input.next);
    CloseHandle(input.ready);
    input.thread = NULL;
}
