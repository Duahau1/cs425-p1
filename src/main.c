#include "lab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef TEST
#define main main_exclude
#endif

int main(int argc, char *argv[])
{
    if (argc == 1)
    {
        print_manual();
        return EXIT_SUCCESS;
    }

    REQUEST_HEADER *request = parse_opt(argc, argv, OPT_STRING);
    if (request == NULL || request->host == NULL ||
        request->sender == NULL || request->receiver == NULL)
    {
        print_manual();
        free(request);
        return 1;
    }

    char body[BUF_SIZE] = "";
    if (request->body == NULL)
    {
        char line[256];
        size_t body_length = 0;

        while (fgets(line, sizeof(line), stdin) != NULL && line[0] != '\n')
        {
            size_t line_length = strlen(line);
            if (line_length > BUF_SIZE - body_length - 1)
            {
                fprintf(stderr, "Mail body is too long.\r\n");
                free(request);
                return EXIT_FAILURE;
            }

            memcpy(body + body_length, line, line_length);
            body_length += line_length;
            body[body_length] = '\0';
        }
        request->body = body;
    }

    int sock = init_socket(request->host, request->port);
    if (sock < 0)
    {
        free(request);
        return 2;
    }

    char cmd[BUF_SIZE];

    // 2. read greeting
    if (smtp_command(sock, NULL, 220) < 0)
    {
        close(sock);
        free(request);
        return 2;
    }

    // 3. SMTP session
    snprintf(cmd, BUF_SIZE, "HELO %s\r\n", request->helo_host);
    if (smtp_command(sock, cmd, 250) < 0)
    {
        close(sock);
        free(request);
        return 2;
    }

    snprintf(cmd, BUF_SIZE, "MAIL FROM:<%s>\r\n", request->sender);
    if (smtp_command(sock, cmd, 250) < 0)
    {
        close(sock);
        free(request);
        return 2;
    }

    snprintf(cmd, BUF_SIZE, "RCPT TO:<%s>\r\n", request->receiver);
    if (smtp_command(sock, cmd, 250) < 0)
    {
        close(sock);
        free(request);
        return 2;
    }

    if (smtp_command(sock, "DATA\r\n", 354) < 0)
    {
        close(sock);
        free(request);
        return 2;
    }

    // 4. headers and body
    char *stuffed_body = dot_stuff_body(request->body);
    if (stuffed_body == NULL)
    {
        close(sock);
        free(request);
        return EXIT_FAILURE;
    }

    int message_length = snprintf(cmd, BUF_SIZE,
                                  "Subject: %s\r\n"
                                  "From: %s\r\n"
                                  "To: %s\r\n"
                                  "\r\n" // blank line ends headers
                                  "%s\r\n"
                                  ".\r\n", // line with only a period ends the body
                                  request->subject, request->sender, request->receiver, stuffed_body);
    free(stuffed_body);
    if (message_length < 0 || message_length >= BUF_SIZE)
    {
        fprintf(stderr, "Mail message is too long.\r\n");
        close(sock);
        free(request);
        return EXIT_FAILURE;
    }
    if (smtp_command(sock, cmd, 250) < 0)
    {
        close(sock);
        free(request);
        return 2;
    }

    // 5. end session
    if (smtp_command(sock, "QUIT\r\n", 221) < 0)
    {
        close(sock);
        free(request);
        return 2;
    }
    free(request);
    close(sock);

    return 0;
}