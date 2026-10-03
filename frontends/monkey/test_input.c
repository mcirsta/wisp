/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monkey/input.h"

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                                            \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

static HANDLE start_input(void)
{
    HANDLE read, write;
    CHECK(CreatePipe(&read, &write, NULL, 0));
    CHECK(SetStdHandle(STD_INPUT_HANDLE, read));
    CHECK(monkey_input_start());
    return write;
}

static void send_input(HANDLE write, const char *text)
{
    DWORD written;
    CHECK(WriteFile(write, text, strlen(text), &written, NULL));
    CHECK(written == strlen(text));
}

static char *receive_input(void)
{
    CHECK(WaitForSingleObject(monkey_input_event(), 2000) == WAIT_OBJECT_0);
    return monkey_input_command();
}

int main(void)
{
    HANDLE write = start_input();
    send_input(write, "QU");
    CHECK(WaitForSingleObject(monkey_input_event(), 20) == WAIT_TIMEOUT);
    send_input(write, "IT\n");
    CHECK(strcmp(receive_input(), "QUIT\n") == 0);
    /* QUIT must join while the writer remains open and no further data comes. */
    monkey_input_finish();
    CloseHandle(GetStdHandle(STD_INPUT_HANDLE));
    CloseHandle(write);

    write = start_input();
    send_input(write, "one\ntwo");
    CloseHandle(write);
    CHECK(strcmp(receive_input(), "one\n") == 0);
    monkey_input_next();
    CHECK(strcmp(receive_input(), "two") == 0);
    monkey_input_next();
    CHECK(receive_input() == NULL);
    monkey_input_finish();
    CloseHandle(GetStdHandle(STD_INPUT_HANDLE));
    puts("Windows input: partial lines, open-pipe quit, final line and EOF passed");
    return 0;
}
