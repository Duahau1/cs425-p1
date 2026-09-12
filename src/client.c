#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "utils.h"
#include "lab.h"

const char *OPT_STRING = "f:t:s:b:p:H:";
const char *clientOption[OPTION_COUNT] = {
    [OPTION_FROM] = "f",
    [OPTION_TO] = "t",
    [OPTION_SUBJECT] = "s",
    [OPTION_BODY] = "b",
    [OPTION_PORT] = "p",
    [OPTION_HOST] = "H"};

void print_manual(void)
{
    puts("Usage: myapp -f <from> -t <to> [-s subject] [-b body] [-p port]"
         " [-H helo-host] <server>");
    puts("");
    puts("Options:");
    puts("  -f <from>       Envelope sender, for example you@example.com");
    puts("  -t <to>         Envelope recipient");
    puts("  -s <subject>    Subject line (default: empty)");
    puts("  -b <body>       Message body (default: read from stdin)");
    puts("  -p <port>       Port or service name (default: 25)");
    puts("  -H <helo-host>  Host name sent with HELO (default: localhost)");
    puts("  <server>        Host name or address of the mail server");
}

REQUEST_HEADER *parse_opt(int argc, char *const argv[], const char *optstring)
{
    int option;
    int option_index;
    char *from = NULL;
    char *to = NULL;
    char *subject = "";
    int port = DEFAULT_PORT;
    char *helo_server = DEFAULT_HELO_SERVER;
    char *server = NULL;
    char *body = NULL;
    REQUEST_HEADER *requestHeader = malloc(sizeof(*requestHeader));

    if (argc < 1 || argv == NULL || optstring == NULL || requestHeader == NULL)
    {
        free(requestHeader);
        return NULL;
    }

    opterr = 0;
    optind = 1;

    while ((option = getopt(argc, argv, optstring)) != -1)
    {
        if (option == '?' || option == ':')
        {
            free(requestHeader);
            return NULL;
        }

        option_index = -1;
        for (int index = 0; index < OPTION_COUNT; index++)
        {
            if (option == clientOption[index][0])
            {
                option_index = index;
                break;
            }
        }

        switch (option_index)
        {

        case OPTION_FROM:
            from = optarg;
            break;

        case OPTION_TO:
            to = optarg;
            break;

        case OPTION_SUBJECT:
            subject = optarg;
            break;
        case OPTION_BODY:
            body = optarg;
            break;
        case OPTION_PORT:
            port = atoi(optarg);
            break;
        case OPTION_HOST:
            if (optind < argc && argv[optind][0] != '-')
            {
                helo_server = optarg;
                server = argv[optind++];
            }
            else
            {
                server = optarg;
            }
            break;
        }
    }

    if (server == NULL && optind < argc)
    {
        server = argv[optind++];
    }

    requestHeader->sender = from;
    requestHeader->receiver = to;
    requestHeader->subject = subject;
    requestHeader->body = body;
    requestHeader->port = port;
    requestHeader->helo_host = helo_server;
    requestHeader->host = server;

    return requestHeader;
}

char *dot_stuff_body(const char *body)
{
    size_t body_length;
    size_t extra_periods = 0;
    size_t output_length;
    size_t input_index;
    size_t output_index = 0;
    int at_line_start = 1;
    char *stuffed_body;

    if (body == NULL)
    {
        return NULL;
    }

    body_length = strlen(body);
    for (input_index = 0; input_index < body_length; input_index++)
    {
        if (at_line_start && body[input_index] == '.')
        {
            extra_periods++;
        }
        at_line_start = body[input_index] == '\n';
    }

    output_length = body_length + extra_periods + 1;
    stuffed_body = malloc(output_length);
    if (stuffed_body == NULL)
    {
        return NULL;
    }

    at_line_start = 1;
    for (input_index = 0; input_index < body_length; input_index++)
    {
        if (at_line_start && body[input_index] == '.')
        {
            stuffed_body[output_index++] = '.';
        }
        stuffed_body[output_index++] = body[input_index];
        at_line_start = body[input_index] == '\n';
    }
    stuffed_body[output_index] = '\0';

    return stuffed_body;
}

int init_socket(const char *host, int port)
{
    int sock_fd;

    if (port < 0 || port > UINT16_MAX)
    {
        fprintf(stderr, "Invalid port.\r\n");
        return -1;
    }

    struct hostent *server = gethostbyname(host);
    if (server == NULL)
    {
        fprintf(stderr, "Host lookup failed.\r\n");
        return -1;
    }

    if (server->h_length <= 0 ||
        (size_t)server->h_length > sizeof(((struct sockaddr_in *)0)->sin_addr.s_addr))
    {
        fprintf(stderr, "Invalid host address length.\r\n");
        return -1;
    }

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        perror("Socket init failed.\r\n");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr.s_addr, server->h_addr, (size_t)server->h_length);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("Socket connection failed.\r\n");
        close(sock_fd);
        return -1;
    }

    return sock_fd;
}

static ssize_t socket_send(void *context, const void *buffer, size_t length)
{
    int socket_fd = *(int *)context;

    return send(socket_fd, buffer, length, 0);
}

static ssize_t socket_receive(void *context, void *buffer, size_t length)
{
    int socket_fd = *(int *)context;

    return recv(socket_fd, buffer, length, 0);
}

SMTP_TRANSPORT socket_transport(int *socket_fd)
{
    SMTP_TRANSPORT transport = {
        .context = socket_fd,
        .send = socket_send,
        .receive = socket_receive};

    return transport;
}

int smtp_command_with_transport(const SMTP_TRANSPORT *transport,
                                const char *cmd, int expected_code)
{
    char buf[BUF_SIZE];
    int line_length = 0;
    int reply_code = -1;

    if (transport == NULL || transport->send == NULL || transport->receive == NULL)
    {
        return -1;
    }

    if (cmd != NULL)
    {
        if (transport->send(transport->context, cmd, strlen(cmd)) < 0)
        {
            perror("SMTP send failed");
            return -1;
        }
        printf("C: %s", cmd);
    }

    while (line_length < BUF_SIZE - 1)
    {
        char character;
        ssize_t received = transport->receive(transport->context, &character, 1);

        if (received <= 0)
        {
            fprintf(stderr, "SMTP receive failed before a complete reply\n");
            return -1;
        }

        buf[line_length++] = character;

        if (character == '\n')
        {
            if (line_length < 4 ||
                buf[0] < '0' || buf[0] > '9' ||
                buf[1] < '0' || buf[1] > '9' ||
                buf[2] < '0' || buf[2] > '9')
            {
                buf[line_length] = '\0';
                fprintf(stderr, "Invalid SMTP reply from server: %s", buf);
                return -1;
            }

            buf[line_length] = '\0';
            printf("S: %s", buf);

            reply_code = (buf[0] - '0') * 100 +
                         (buf[1] - '0') * 10 +
                         (buf[2] - '0');

            if (buf[3] != '-' && buf[3] != ' ')
            {
                fprintf(stderr, "Invalid SMTP reply delimiter from server: %s", buf);
                return -1;
            }

            line_length = 0;

            if (buf[3] == ' ')
            {
                break;
            }
        }
    }

    if (line_length != 0)
    {
        fprintf(stderr, "SMTP reply is too long for the receive buffer\r\n");
        return -1;
    }

    if (expected_code != 0 && reply_code != expected_code)
    {
        fprintf(stderr, "Unexpected SMTP reply: expected %d, server sent %d\r\n",
                expected_code, reply_code);
        return -1;
    }

    return reply_code;
}

int smtp_command(int socket_fd, const char *cmd, int expected_code)
{
    SMTP_TRANSPORT transport = socket_transport(&socket_fd);

    return smtp_command_with_transport(&transport, cmd, expected_code);
}