#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "harness/unity.h"

#include "../src/client.h"
#include "../src/smtp.h"
#include "../src/utils.h"

void setUp(void)
{
}

void tearDown(void)
{
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
