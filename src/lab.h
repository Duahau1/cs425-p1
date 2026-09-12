#ifndef CLIENT_H
#define CLIENT_H

#include <stddef.h>
#include <sys/types.h>

#define BUF_SIZE 4096
#define DEFAULT_PORT 2525
#define DEFAULT_HELO_SERVER "localhost"

typedef enum
{
    OPTION_FROM,
    OPTION_TO,
    OPTION_SUBJECT,
    OPTION_BODY,
    OPTION_PORT,
    OPTION_HOST,
    OPTION_COUNT
} CLIENT_OPTION;

extern const char *clientOption[OPTION_COUNT];

typedef struct
{
    char *sender;
    char *receiver;
    char *host;
    char *helo_host;
    char *subject;
    char *body;
    int port;
} REQUEST_HEADER;

typedef ssize_t (*SMTP_SEND_FUNCTION)(void *context, const void *buffer, size_t length);
typedef ssize_t (*SMTP_RECEIVE_FUNCTION)(void *context, void *buffer, size_t length);

typedef struct
{
    void *context;
    SMTP_SEND_FUNCTION send;
    SMTP_RECEIVE_FUNCTION receive;
} SMTP_TRANSPORT;

extern const char *OPT_STRING;

void print_manual(void);

REQUEST_HEADER *parse_opt(int argc, char *const argv[], const char *optstring);

char *dot_stuff_body(const char *body);

int init_socket(const char *host, int port);

SMTP_TRANSPORT socket_transport(int *socket_fd);

int smtp_command_with_transport(const SMTP_TRANSPORT *transport,
                                const char *cmd, int expected_code);

int smtp_command(int sock, const char *cmd, int expected_code);

#endif // CLIENT_H
