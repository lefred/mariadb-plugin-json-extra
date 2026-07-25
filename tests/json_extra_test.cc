/* Copyright (c) 2026 lefred (Frédéric Descamps) */

#include "json_extra.h"
#include <cstdlib>
#include <iostream>
#include <string>

static void expect_patch(const std::string &doc, const std::string &patch,
                         const std::string &expected)
{
  std::string result, error;
  if (!json_extra::patch(doc, patch, &result, &error) || result != expected)
  {
    std::cerr << "patch failed: " << error << "\ngot: " << result
              << "\nexpected: " << expected << '\n';
    std::exit(1);
  }
}

static void expect_error(const std::string &doc, const std::string &patch)
{
  std::string result, error;
  if (json_extra::patch(doc, patch, &result, &error))
  { std::cerr << "expected patch failure\n"; std::exit(1); }
}

int main()
{
  expect_patch("{\"foo\":\"bar\"}", "[{\"op\":\"add\",\"path\":\"/baz\",\"value\":\"qux\"}]",
               "{\"baz\":\"qux\",\"foo\":\"bar\"}");
  expect_patch("[\"a\",\"b\"]", "[{\"op\":\"add\",\"path\":\"/1\",\"value\":\"x\"}]",
               "[\"a\",\"x\",\"b\"]");
  expect_patch("{\"a\":1,\"b\":2}", "[{\"op\":\"remove\",\"path\":\"/a\"}]",
               "{\"b\":2}");
  expect_patch("{\"a\":1}", "[{\"op\":\"replace\",\"path\":\"/a\",\"value\":2}]",
               "{\"a\":2}");
  expect_patch("{\"a\":{\"x\":1}}", "[{\"op\":\"copy\",\"from\":\"/a\",\"path\":\"/b\"}]",
               "{\"a\":{\"x\":1},\"b\":{\"x\":1}}");
  expect_patch("[\"a\",\"b\",\"c\"]", "[{\"op\":\"move\",\"from\":\"/0\",\"path\":\"/2\"}]",
               "[\"b\",\"c\",\"a\"]");
  expect_patch("{\"a\":1}", "[{\"op\":\"test\",\"path\":\"/a\",\"value\":1.0}]",
               "{\"a\":1}");
  expect_patch("{\"a\":1e9999}",
               "[{\"op\":\"test\",\"path\":\"/a\",\"value\":10e9998}]",
               "{\"a\":1e9999}");
  expect_error("{\"a\":1e9999}",
               "[{\"op\":\"test\",\"path\":\"/a\",\"value\":2e9999}]");
  expect_patch("{\"a/b\":{\"~x\":1}}",
               "[{\"op\":\"replace\",\"path\":\"/a~1b/~0x\",\"value\":2}]",
               "{\"a/b\":{\"~x\":2}}");
  expect_patch("1", "[{\"op\":\"replace\",\"path\":\"\",\"value\":{\"x\":2}}]",
               "{\"x\":2}");
  expect_error("{\"a\":1}", "[{\"op\":\"test\",\"path\":\"/a\",\"value\":2}]");
  expect_error("{}", "[{\"op\":\"remove\",\"path\":\"/missing\"}]");

  std::string delta, result, error;
  const std::string from= "{\"a\":1,\"gone\":true,\"list\":[1,2]}";
  const std::string to= "{\"a\":2,\"new\":null,\"list\":[1,3,4]}";
  if (!json_extra::diff(from, to, &delta, &error) ||
      !json_extra::patch(from, delta, &result, &error) ||
      result != "{\"a\":2,\"list\":[1,3,4],\"new\":null}")
  {
    std::cerr << "diff round trip failed: " << error << "\npatch: " << delta
              << "\nresult: " << result << '\n';
    return 1;
  }
  return 0;
}
