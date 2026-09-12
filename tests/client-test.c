#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "harness/unity.h"

#include "../src/lab.h"
#include "../src/smtp.h"
#include "../src/utils.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static int mock_socket_should_fail;
static int mock_send_should_fail;

int socket(int domain, int type, int protocol)
{
  static int (*real_socket)(int, int, int);

  if (mock_socket_should_fail)
  {
    return -1;
  }

  if (real_socket == NULL)
  {
    real_socket = (int (*)(int, int, int))dlsym(RTLD_NEXT, "socket");
  }

  return real_socket(domain, type, protocol);
}

ssize_t send(int socket_fd, const void *buffer, size_t length, int flags)
{
  static ssize_t (*real_send)(int, const void *, size_t, int);

  if (mock_send_should_fail)
  {
    return -1;
  }

  if (real_send == NULL)
  {
    real_send = (ssize_t (*)(int, const void *, size_t, int))dlsym(RTLD_NEXT, "send");
  }

  return real_send(socket_fd, buffer, length, flags);
}

void test_print_manual_outputs_usage_and_options(void)
{
  const char *expected =
      "Usage: myapp -f <from> -t <to> [-s subject] [-b body] [-p port]"
      " [-H helo-host] <server>\n"
      "\n"
      "Options:\n"
      "  -f <from>       Envelope sender, for example you@example.com\n"
      "  -t <to>         Envelope recipient\n"
      "  -s <subject>    Subject line (default: empty)\n"
      "  -b <body>       Message body (default: read from stdin)\n"
      "  -p <port>       Port or service name (default: 25)\n"
      "  -H <helo-host>  Host name sent with HELO (default: localhost)\n"
      "  <server>        Host name or address of the mail server\n";
  FILE *output = tmpfile();
  int saved_stdout;
  char actual[1024];
  size_t output_length;

  TEST_ASSERT_NOT_NULL(output);
  saved_stdout = dup(STDOUT_FILENO);
  TEST_ASSERT_NOT_EQUAL(-1, saved_stdout);
  TEST_ASSERT_NOT_EQUAL(-1, dup2(fileno(output), STDOUT_FILENO));

  print_manual();
  fflush(stdout);
  TEST_ASSERT_NOT_EQUAL(-1, dup2(saved_stdout, STDOUT_FILENO));
  close(saved_stdout);

  rewind(output);
  output_length = fread(actual, 1, sizeof(actual) - 1, output);
  actual[output_length] = '\0';
  fclose(output);

  TEST_ASSERT_EQUAL_STRING(expected, actual);
}

void test_init_socket_returns_error_for_unknown_host(void)
{
  TEST_ASSERT_EQUAL_INT(-1, init_socket("host-does-not-exist.invalid", 25));
}

void test_init_socket_returns_error_when_connection_fails(void)
{
  TEST_ASSERT_EQUAL_INT(-1, init_socket("127.0.0.1", 0));
}

void test_init_socket_returns_error_when_socket_creation_fails(void)
{
  mock_socket_should_fail = 1;

  TEST_ASSERT_EQUAL_INT(-1, init_socket("127.0.0.1", 25));

  mock_socket_should_fail = 0;
}

void test_init_socket_connects_to_listening_server(void)
{
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in address;
  socklen_t address_length = sizeof(address);
  int client;
  int accepted;

  TEST_ASSERT_NOT_EQUAL(-1, listener);
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(0);
  TEST_ASSERT_EQUAL_INT(0, bind(listener, (struct sockaddr *)&address, sizeof(address)));
  TEST_ASSERT_EQUAL_INT(0, getsockname(listener, (struct sockaddr *)&address, &address_length));
  TEST_ASSERT_EQUAL_INT(0, listen(listener, 1));

  client = init_socket("127.0.0.1", ntohs(address.sin_port));
  TEST_ASSERT_NOT_EQUAL(-1, client);
  accepted = accept(listener, NULL, NULL);
  TEST_ASSERT_NOT_EQUAL(-1, accepted);

  close(accepted);
  close(client);
  close(listener);
}

void test_smtp_command_sends_command_and_reads_reply(void)
{
  int sockets[2];
  char command_buffer[32];
  const char *reply = "250 OK\r\n";
  ssize_t command_length;

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  TEST_ASSERT_EQUAL_INT((int)strlen(reply),
                        (int)send(sockets[1], reply, strlen(reply), 0));
  TEST_ASSERT_EQUAL_INT(250, smtp_command(sockets[0], "NOOP\r\n", 250));

  command_length = recv(sockets[1], command_buffer, sizeof(command_buffer), 0);
  TEST_ASSERT_EQUAL_INT(6, command_length);
  command_buffer[command_length] = '\0';
  TEST_ASSERT_EQUAL_STRING("NOOP\r\n", command_buffer);

  close(sockets[0]);
  close(sockets[1]);
}

void test_smtp_command_returns_error_when_send_fails(void)
{
  int sockets[2];

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  mock_send_should_fail = 1;

  TEST_ASSERT_EQUAL_INT(-1, smtp_command(sockets[0], "NOOP\r\n", 0));

  mock_send_should_fail = 0;
  close(sockets[0]);
  close(sockets[1]);
}

void test_smtp_command_with_transport_rejects_invalid_transport(void)
{
  int socket_fd = -1;
  SMTP_TRANSPORT transport = socket_transport(&socket_fd);

  TEST_ASSERT_EQUAL_INT(-1, smtp_command_with_transport(NULL, NULL, 0));

  transport.send = NULL;
  TEST_ASSERT_EQUAL_INT(-1, smtp_command_with_transport(&transport, NULL, 0));

  transport = socket_transport(&socket_fd);
  transport.receive = NULL;
  TEST_ASSERT_EQUAL_INT(-1, smtp_command_with_transport(&transport, NULL, 0));
}

void test_smtp_command_reads_reply_without_sending_command(void)
{
  int sockets[2];
  const char *reply = "220 Ready\r\n";

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  TEST_ASSERT_EQUAL_INT((int)strlen(reply),
                        (int)send(sockets[1], reply, strlen(reply), 0));
  TEST_ASSERT_EQUAL_INT(220, smtp_command(sockets[0], NULL, 220));

  close(sockets[0]);
  close(sockets[1]);
}

void test_smtp_command_reads_multiline_reply(void)
{
  int sockets[2];
  const char *reply = "250-first line\r\n250-second line\r\n250 final\r\n";

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  TEST_ASSERT_EQUAL_INT((int)strlen(reply),
                        (int)send(sockets[1], reply, strlen(reply), 0));
  TEST_ASSERT_EQUAL_INT(250, smtp_command(sockets[0], NULL, 250));

  close(sockets[0]);
  close(sockets[1]);
}

void test_smtp_command_accepts_any_reply_when_expected_code_is_zero(void)
{
  int sockets[2];
  const char *reply = "550 Not available\r\n";

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  TEST_ASSERT_EQUAL_INT((int)strlen(reply),
                        (int)send(sockets[1], reply, strlen(reply), 0));
  TEST_ASSERT_EQUAL_INT(550, smtp_command(sockets[0], NULL, 0));

  close(sockets[0]);
  close(sockets[1]);
}

void test_smtp_command_rejects_invalid_reply_code(void)
{
  int sockets[2];
  const char *reply = "25x Invalid\r\n";

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  TEST_ASSERT_EQUAL_INT((int)strlen(reply),
                        (int)send(sockets[1], reply, strlen(reply), 0));
  TEST_ASSERT_EQUAL_INT(-1, smtp_command(sockets[0], NULL, 0));

  close(sockets[0]);
  close(sockets[1]);
}

void test_smtp_command_rejects_invalid_reply_delimiter(void)
{
  int sockets[2];
  const char *reply = "250x Invalid\r\n";

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  TEST_ASSERT_EQUAL_INT((int)strlen(reply),
                        (int)send(sockets[1], reply, strlen(reply), 0));
  TEST_ASSERT_EQUAL_INT(-1, smtp_command(sockets[0], NULL, 0));

  close(sockets[0]);
  close(sockets[1]);
}

void test_smtp_command_rejects_disconnected_server(void)
{
  int sockets[2];

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  close(sockets[1]);
  TEST_ASSERT_EQUAL_INT(-1, smtp_command(sockets[0], NULL, 0));

  close(sockets[0]);
}

void test_smtp_command_rejects_oversized_reply(void)
{
  int sockets[2];
  char reply[BUF_SIZE];

  memset(reply, 'A', sizeof(reply));
  reply[sizeof(reply) - 1] = '\n';
  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  TEST_ASSERT_EQUAL_INT((int)sizeof(reply),
                        (int)send(sockets[1], reply, sizeof(reply), 0));
  TEST_ASSERT_EQUAL_INT(-1, smtp_command(sockets[0], NULL, 0));

  close(sockets[0]);
  close(sockets[1]);
}

void test_smtp_command_rejects_unexpected_reply_code(void)
{
  int sockets[2];
  const char *reply = "250 OK\r\n";

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  TEST_ASSERT_EQUAL_INT((int)strlen(reply),
                        (int)send(sockets[1], reply, strlen(reply), 0));
  TEST_ASSERT_EQUAL_INT(-1, smtp_command(sockets[0], NULL, 220));

  close(sockets[0]);
  close(sockets[1]);
}

void test_parse_opt_rejects_invalid_arguments(void)
{
  char *argv[] = {"myapp"};

  TEST_ASSERT_NULL(parse_opt(0, argv, OPT_STRING));
  TEST_ASSERT_NULL(parse_opt(1, NULL, OPT_STRING));
  TEST_ASSERT_NULL(parse_opt(1, argv, NULL));
}

void test_parse_opt_uses_defaults(void)
{
  char *argv[] = {"myapp", "server.example"};
  REQUEST_HEADER *request = parse_opt(2, argv, OPT_STRING);

  TEST_ASSERT_NOT_NULL(request);
  TEST_ASSERT_NULL(request->sender);
  TEST_ASSERT_NULL(request->receiver);
  TEST_ASSERT_EQUAL_STRING("server.example", request->host);
  TEST_ASSERT_EQUAL_STRING("localhost", request->helo_host);
  TEST_ASSERT_EQUAL_STRING("", request->subject);
  TEST_ASSERT_NULL(request->body);
  TEST_ASSERT_EQUAL_INT(DEFAULT_PORT, request->port);

  free(request);
}

void test_parse_opt_reads_all_options(void)
{
  char *argv[] = {"myapp", "-f", "from@example.com", "-t", "to@example.com",
                  "-s", "Subject", "-b", "Body", "-p", "2526", "-H", "helo.example",
                  "server.example"};
  REQUEST_HEADER *request = parse_opt(14, argv, OPT_STRING);

  TEST_ASSERT_NOT_NULL(request);
  TEST_ASSERT_EQUAL_STRING("from@example.com", request->sender);
  TEST_ASSERT_EQUAL_STRING("to@example.com", request->receiver);
  TEST_ASSERT_EQUAL_STRING("server.example", request->host);
  TEST_ASSERT_EQUAL_STRING("helo.example", request->helo_host);
  TEST_ASSERT_EQUAL_STRING("Subject", request->subject);
  TEST_ASSERT_EQUAL_STRING("Body", request->body);
  TEST_ASSERT_EQUAL_INT(2526, request->port);

  free(request);
}

void test_parse_opt_accepts_host_from_H_without_server(void)
{
  char *argv[] = {"myapp", "-H", "server.example"};
  REQUEST_HEADER *request = parse_opt(3, argv, OPT_STRING);

  TEST_ASSERT_NOT_NULL(request);
  TEST_ASSERT_EQUAL_STRING("server.example", request->host);
  TEST_ASSERT_EQUAL_STRING("localhost", request->helo_host);

  free(request);
}

void test_parse_opt_accepts_empty_values(void)
{
  char *argv[] = {"myapp", "-f", "", "-t", "", "-s", "", "-b", "", "-p", "", "server"};
  REQUEST_HEADER *request = parse_opt(12, argv, OPT_STRING);

  TEST_ASSERT_NOT_NULL(request);
  TEST_ASSERT_EQUAL_STRING("", request->sender);
  TEST_ASSERT_EQUAL_STRING("", request->receiver);
  TEST_ASSERT_EQUAL_STRING("", request->subject);
  TEST_ASSERT_EQUAL_STRING("", request->body);
  TEST_ASSERT_EQUAL_INT(0, request->port);
  TEST_ASSERT_EQUAL_STRING("server", request->host);

  free(request);
}

void test_parse_opt_last_repeated_option_wins(void)
{
  char *argv[] = {"myapp", "-f", "first", "-f", "last", "-t", "one", "-t", "two",
                  "-s", "first subject", "-s", "last subject", "-b", "first body", "-b",
                  "last body", "-p", "25", "-p", "587", "server"};
  REQUEST_HEADER *request = parse_opt(22, argv, OPT_STRING);

  TEST_ASSERT_NOT_NULL(request);
  TEST_ASSERT_EQUAL_STRING("last", request->sender);
  TEST_ASSERT_EQUAL_STRING("two", request->receiver);
  TEST_ASSERT_EQUAL_STRING("last subject", request->subject);
  TEST_ASSERT_EQUAL_STRING("last body", request->body);
  TEST_ASSERT_EQUAL_INT(587, request->port);

  free(request);
}

void test_parse_opt_ignores_extra_positional_arguments(void)
{
  char *argv[] = {"myapp", "first.example", "second.example"};
  REQUEST_HEADER *request = parse_opt(3, argv, OPT_STRING);

  TEST_ASSERT_NOT_NULL(request);
  TEST_ASSERT_EQUAL_STRING("first.example", request->host);

  free(request);
}

void test_parse_opt_leaves_host_null_when_server_is_missing(void)
{
  char *argv[] = {"myapp", "-f", "from@example.com", "-t", "to@example.com"};
  REQUEST_HEADER *request = parse_opt(5, argv, OPT_STRING);

  TEST_ASSERT_NOT_NULL(request);
  TEST_ASSERT_NULL(request->host);
  TEST_ASSERT_EQUAL_STRING("from@example.com", request->sender);
  TEST_ASSERT_EQUAL_STRING("to@example.com", request->receiver);

  free(request);
}

void test_parse_opt_rejects_unknown_and_missing_arguments(void)
{
  char *unknown[] = {"myapp", "-x"};
  char *missing[] = {"myapp", "-f"};
  char *missing_with_colon[] = {"myapp", "-f"};

  TEST_ASSERT_NULL(parse_opt(2, unknown, OPT_STRING));
  TEST_ASSERT_NULL(parse_opt(2, missing, OPT_STRING));
  TEST_ASSERT_NULL(parse_opt(2, missing_with_colon, ":f:t:s:b:p:H:"));
}

void test_parse_opt_can_be_called_repeatedly(void)
{
  char *first_argv[] = {"myapp", "first.example"};
  char *second_argv[] = {"myapp", "second.example"};
  REQUEST_HEADER *first = parse_opt(2, first_argv, OPT_STRING);
  REQUEST_HEADER *second = parse_opt(2, second_argv, OPT_STRING);

  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_STRING("first.example", first->host);
  TEST_ASSERT_EQUAL_STRING("second.example", second->host);

  free(first);
  free(second);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_print_manual_outputs_usage_and_options);
  RUN_TEST(test_init_socket_returns_error_for_unknown_host);
  RUN_TEST(test_init_socket_returns_error_when_connection_fails);
  RUN_TEST(test_init_socket_returns_error_when_socket_creation_fails);
  RUN_TEST(test_init_socket_connects_to_listening_server);
  RUN_TEST(test_smtp_command_sends_command_and_reads_reply);
  RUN_TEST(test_smtp_command_returns_error_when_send_fails);
  RUN_TEST(test_smtp_command_with_transport_rejects_invalid_transport);
  RUN_TEST(test_smtp_command_reads_reply_without_sending_command);
  RUN_TEST(test_smtp_command_reads_multiline_reply);
  RUN_TEST(test_smtp_command_accepts_any_reply_when_expected_code_is_zero);
  RUN_TEST(test_smtp_command_rejects_invalid_reply_code);
  RUN_TEST(test_smtp_command_rejects_invalid_reply_delimiter);
  RUN_TEST(test_smtp_command_rejects_disconnected_server);
  RUN_TEST(test_smtp_command_rejects_oversized_reply);
  RUN_TEST(test_smtp_command_rejects_unexpected_reply_code);
  RUN_TEST(test_parse_opt_rejects_invalid_arguments);
  RUN_TEST(test_parse_opt_uses_defaults);
  RUN_TEST(test_parse_opt_reads_all_options);
  RUN_TEST(test_parse_opt_accepts_host_from_H_without_server);
  RUN_TEST(test_parse_opt_accepts_empty_values);
  RUN_TEST(test_parse_opt_last_repeated_option_wins);
  RUN_TEST(test_parse_opt_ignores_extra_positional_arguments);
  RUN_TEST(test_parse_opt_leaves_host_null_when_server_is_missing);
  RUN_TEST(test_parse_opt_rejects_unknown_and_missing_arguments);
  RUN_TEST(test_parse_opt_can_be_called_repeatedly);
  return UNITY_END();
}
