#ifndef SMTP_H
#define SMTP_H

#include <stddef.h>
#include <stdio.h>

typedef enum
{
    SERVICE_READY = 220,
    OK = 250,
    START_MAIL_INPUT = 354,
    SERVICE_CLOSING = 221
} SMTP_STATUS_CODE;

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
    [QUIT] = "QUIT"
};

/**
 * Dynamically constructs an SMTP response.
 * Automatically appends \r\n and enforces buffer safety.
 */
static void build_smtp_response(char *dest_buffer, size_t dest_size,
                                SMTP_STATUS_CODE status_code, const char *middle_text, const char *tail_text)
{
    if (dest_buffer == NULL || dest_size == 0)
        return;

    if (middle_text == NULL)
        middle_text = "";
    if (tail_text == NULL)
        tail_text = "";

    snprintf(dest_buffer, dest_size, "%d %s %s\r\n", status_code, middle_text, tail_text);
}

#endif