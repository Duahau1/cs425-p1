#ifndef SMTP_H
#define SMTP_H

#include <stddef.h>
#include <stdio.h>

typedef enum
{
    HELO,
    MAIL,
    RCPT,
    DATA,
    QUIT,
    SMTP_COMMAND_COUNT
} SMTP_COMMAND_SEMANTICS;

static const char *SMTP_COMMAND_NAMES[SMTP_COMMAND_COUNT] = {
    [HELO] = "HELO",
    [MAIL] = "MAIL",
    [RCPT] = "RCPT",
    [DATA] = "DATA",
    [QUIT] = "QUIT"};

#endif