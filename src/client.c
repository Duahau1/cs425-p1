#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "utils.h"
#include "smtp.h"
#include "lab.h"

const char *OPT_STRING = "f:t:s:b:p:H:";

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
        switch (option)
        {

        case 'f':
            from = optarg;
            break;

        case 't':
            to = optarg;
            break;

        case 's':
            subject = optarg;
            break;
        case 'b':
            body = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 'H':
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

int init_socket(const char *host, int port)
{
    int sock_fd;

    struct hostent *server = gethostbyname(host);
    if (server == NULL)
    {
        fprintf(stderr, "Host lookup failed.\r\n");
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
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("Socket connection failed.\r\n");
        close(sock_fd);
        return -1;
    }

    return sock_fd;
}

int smtp_command(int socket_fd, const char *cmd, int expected_code)
{
    char buf[BUF_SIZE];
    int line_length = 0;
    int reply_code = -1;

    if (cmd != NULL)
    {
        if (send(socket_fd, cmd, strlen(cmd), 0) < 0)
        {
            perror("SMTP send failed");
            return -1;
        }
        printf("C: %s", cmd);
    }

    while (line_length < BUF_SIZE - 1)
    {
        char character;
        ssize_t received = recv(socket_fd, &character, 1, 0);

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
        fprintf(stderr, "SMTP reply is too long for the receive buffer\n");
        return -1;
    }

    if (expected_code != 0 && reply_code != expected_code)
    {
        fprintf(stderr, "Unexpected SMTP reply: expected %d, server sent %d\n",
                expected_code, reply_code);
        return -1;
    }

    return reply_code;
}