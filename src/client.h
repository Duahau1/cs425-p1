#ifndef CLIENT_H
#define CLIENT_H

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

static const char *clientOption[OPTION_COUNT] = {
    [OPTION_FROM] = "f",
    [OPTION_TO] = "t",
    [OPTION_SUBJECT] = "s",
    [OPTION_BODY] = "b",
    [OPTION_PORT] = "p",
    [OPTION_HOST] = "H"};

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

extern const char *OPT_STRING;

void print_manual(void);

REQUEST_HEADER *parse_opt(int argc, char *const argv[], const char *optstring);

void prepare_smtp();

int init_socket(const char *host, int port);

int smtp_command(int sock, const char *cmd, int expected_code);

#endif // CLIENT_H
