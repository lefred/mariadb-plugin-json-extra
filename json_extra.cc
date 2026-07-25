/* Copyright (c) 2019,2024,2025,2026 MariaDB Corporation
   Copyright (c) 2026 lefred (Frédéric Descamps)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#include "json_extra.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace json_extra {
namespace {

struct Value
{
  enum Type { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT } type= NUL;
  bool boolean= false;
  std::string scalar;
  std::vector<Value> array;
  std::map<std::string, Value> object;

  static Value string(const std::string &s)
  { Value v; v.type= STRING; v.scalar= s; return v; }
  static Value number(const std::string &s)
  { Value v; v.type= NUMBER; v.scalar= s; return v; }
  static Value boolean_value(bool b)
  { Value v; v.type= BOOL; v.boolean= b; return v; }
};

class Parser
{
  const std::string &s;
  size_t p= 0;
  unsigned depth= 0;

  [[noreturn]] void fail(const char *message) const
  {
    std::ostringstream out;
    out << message << " at byte " << p;
    throw std::runtime_error(out.str());
  }
  void ws()
  { while (p < s.size() && (s[p]==' ' || s[p]=='\t' || s[p]=='\n' || s[p]=='\r')) ++p; }
  static void utf8(std::string &out, unsigned cp)
  {
    if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff)
    {
      out.push_back(static_cast<char>(0xc0 | cp >> 6));
      out.push_back(static_cast<char>(0x80 | (cp & 63)));
    }
    else if (cp <= 0xffff)
    {
      out.push_back(static_cast<char>(0xe0 | cp >> 12));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
      out.push_back(static_cast<char>(0x80 | (cp & 63)));
    }
    else
    {
      out.push_back(static_cast<char>(0xf0 | cp >> 18));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
      out.push_back(static_cast<char>(0x80 | (cp & 63)));
    }
  }
  unsigned hex4()
  {
    if (p + 4 > s.size()) fail("incomplete Unicode escape");
    unsigned n= 0;
    for (int i= 0; i < 4; ++i)
    {
      char c= s[p++];
      n <<= 4;
      if (c >= '0' && c <= '9') n+= c-'0';
      else if (c >= 'a' && c <= 'f') n+= c-'a'+10;
      else if (c >= 'A' && c <= 'F') n+= c-'A'+10;
      else fail("invalid Unicode escape");
    }
    return n;
  }
  std::string parse_string()
  {
    if (p >= s.size() || s[p++] != '"') fail("expected string");
    std::string out;
    while (p < s.size())
    {
      unsigned char c= static_cast<unsigned char>(s[p++]);
      if (c == '"') return out;
      if (c < 0x20) fail("control character in string");
      if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
      if (p == s.size()) fail("incomplete escape");
      switch (s[p++])
      {
      case '"': out.push_back('"'); break; case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break; case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break; case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break; case 't': out.push_back('\t'); break;
      case 'u':
        {
          unsigned cp= hex4();
          if (cp >= 0xd800 && cp <= 0xdbff)
          {
            if (p + 2 > s.size() || s[p++] != '\\' || s[p++] != 'u')
              fail("missing low surrogate");
            unsigned low= hex4();
            if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
            cp= 0x10000 + ((cp-0xd800) << 10) + low-0xdc00;
          }
          else if (cp >= 0xdc00 && cp <= 0xdfff) fail("unpaired low surrogate");
          utf8(out, cp);
          break;
        }
      default: fail("invalid escape");
      }
    }
    fail("unterminated string");
  }
  Value value()
  {
    ws();
    if (++depth > 256) fail("maximum nesting depth exceeded");
    Value v;
    if (p == s.size()) fail("expected value");
    if (s[p] == '"') { v= Value::string(parse_string()); }
    else if (s[p] == '{')
    {
      v.type= Value::OBJECT; ++p; ws();
      if (p < s.size() && s[p] == '}') ++p;
      else for (;;)
      {
        ws(); std::string key= parse_string(); ws();
        if (p == s.size() || s[p++] != ':') fail("expected ':'");
        if (v.object.count(key)) fail("duplicate object member");
        v.object.emplace(key, value()); ws();
        if (p < s.size() && s[p] == '}') { ++p; break; }
        if (p == s.size() || s[p++] != ',') fail("expected ','");
      }
    }
    else if (s[p] == '[')
    {
      v.type= Value::ARRAY; ++p; ws();
      if (p < s.size() && s[p] == ']') ++p;
      else for (;;)
      {
        v.array.push_back(value()); ws();
        if (p < s.size() && s[p] == ']') { ++p; break; }
        if (p == s.size() || s[p++] != ',') fail("expected ','");
      }
    }
    else if (s.compare(p, 4, "null") == 0) { p+= 4; }
    else if (s.compare(p, 4, "true") == 0) { p+= 4; v= Value::boolean_value(true); }
    else if (s.compare(p, 5, "false") == 0) { p+= 5; v= Value::boolean_value(false); }
    else
    {
      size_t start= p;
      if (s[p] == '-') ++p;
      if (p == s.size()) fail("invalid number");
      if (s[p] == '0') ++p;
      else
      {
        if (s[p] < '1' || s[p] > '9') fail("invalid number");
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
      }
      if (p < s.size() && s[p] == '.')
      {
        ++p; size_t digits= p;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
        if (digits == p) fail("invalid fraction");
      }
      if (p < s.size() && (s[p] == 'e' || s[p] == 'E'))
      {
        ++p; if (p < s.size() && (s[p] == '+' || s[p] == '-')) ++p;
        size_t digits= p;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
        if (digits == p) fail("invalid exponent");
      }
      if (start == p) fail("expected value");
      v= Value::number(s.substr(start, p-start));
    }
    --depth;
    return v;
  }
public:
  explicit Parser(const std::string &input): s(input) {}
  Value parse()
  {
    Value v= value(); ws();
    if (p != s.size()) fail("trailing data");
    return v;
  }
};

void quote(std::string &out, const std::string &s)
{
  static const char hex[]= "0123456789abcdef";
  out.push_back('"');
  for (unsigned char c: s)
  {
    switch (c)
    {
    case '"': out+= "\\\""; break; case '\\': out+= "\\\\"; break;
    case '\b': out+= "\\b"; break; case '\f': out+= "\\f"; break;
    case '\n': out+= "\\n"; break; case '\r': out+= "\\r"; break;
    case '\t': out+= "\\t"; break;
    default:
      if (c < 0x20) { out+= "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
      else out.push_back(static_cast<char>(c));
    }
  }
  out.push_back('"');
}

void serialize(std::string &out, const Value &v)
{
  switch (v.type)
  {
  case Value::NUL: out+= "null"; break;
  case Value::BOOL: out+= v.boolean ? "true" : "false"; break;
  case Value::NUMBER: out+= v.scalar; break;
  case Value::STRING: quote(out, v.scalar); break;
  case Value::ARRAY:
    out.push_back('[');
    for (size_t i= 0; i < v.array.size(); ++i)
    { if (i) out.push_back(','); serialize(out, v.array[i]); }
    out.push_back(']'); break;
  case Value::OBJECT:
    out.push_back('{');
    for (auto i= v.object.begin(); i != v.object.end(); ++i)
    {
      if (i != v.object.begin()) out.push_back(',');
      quote(out, i->first); out.push_back(':'); serialize(out, i->second);
    }
    out.push_back('}'); break;
  }
}

bool equal(const Value &a, const Value &b)
{
  if (a.type == Value::NUMBER && b.type == Value::NUMBER)
  {
    struct Decimal { bool negative; std::string digits; long long exponent; };
    auto normalize= [](const std::string &text) {
      Decimal n{false, "", 0};
      size_t p= 0, fraction= 0;
      bool after_decimal= false;
      if (text[p] == '-') { n.negative= true; ++p; }
      while (p < text.size() && text[p] != 'e' && text[p] != 'E')
      {
        if (text[p] == '.') { after_decimal= true; ++p; continue; }
        n.digits+= text[p++];
        if (after_decimal) ++fraction;
      }
      n.exponent= -static_cast<long long>(fraction);
      if (p < text.size())
      {
        ++p; bool minus= false;
        if (text[p] == '+' || text[p] == '-') { minus= text[p] == '-'; ++p; }
        long long e= 0;
        while (p < text.size())
        {
          int digit= text[p++]-'0';
          if (e > 100000000000000000LL) e= 1000000000000000000LL;
          else e= e*10+digit;
        }
        n.exponent+= minus ? -e : e;
      }
      size_t first= n.digits.find_first_not_of('0');
      if (first == std::string::npos) return Decimal{false, "0", 0};
      n.digits.erase(0, first);
      while (n.digits.size() > 1 && n.digits.back() == '0')
      { n.digits.pop_back(); ++n.exponent; }
      return n;
    };
    Decimal av= normalize(a.scalar), bv= normalize(b.scalar);
    return av.negative == bv.negative && av.digits == bv.digits &&
           av.exponent == bv.exponent;
  }
  if (a.type != b.type) return false;
  switch (a.type)
  {
  case Value::NUL: return true;
  case Value::BOOL: return a.boolean == b.boolean;
  case Value::NUMBER: case Value::STRING: return a.scalar == b.scalar;
  case Value::ARRAY:
    if (a.array.size() != b.array.size()) return false;
    for (size_t i= 0; i < a.array.size(); ++i)
      if (!equal(a.array[i], b.array[i])) return false;
    return true;
  case Value::OBJECT:
    if (a.object.size() != b.object.size()) return false;
    for (const auto &i: a.object)
    {
      auto j= b.object.find(i.first);
      if (j == b.object.end() || !equal(i.second, j->second)) return false;
    }
    return true;
  }
  return false;
}

std::string token_escape(const std::string &s)
{
  std::string out;
  for (char c: s) { if (c == '~') out+= "~0"; else if (c == '/') out+= "~1"; else out+= c; }
  return out;
}

std::vector<std::string> pointer(const std::string &path)
{
  std::vector<std::string> out;
  if (path.empty()) return out;
  if (path[0] != '/') throw std::runtime_error("JSON Pointer must be empty or start with '/'");
  size_t p= 1;
  for (;;)
  {
    size_t slash= path.find('/', p);
    std::string raw= path.substr(p, slash == std::string::npos ? slash : slash-p), token;
    for (size_t i= 0; i < raw.size(); ++i)
    {
      if (raw[i] != '~') token+= raw[i];
      else if (++i == raw.size() || (raw[i] != '0' && raw[i] != '1'))
        throw std::runtime_error("invalid JSON Pointer escape");
      else token+= raw[i] == '0' ? '~' : '/';
    }
    out.push_back(token);
    if (slash == std::string::npos) break;
    p= slash+1;
  }
  return out;
}

size_t index_of(const std::string &s, size_t size, bool append)
{
  if (append && s == "-") return size;
  if (s.empty() || (s.size() > 1 && s[0] == '0'))
    throw std::runtime_error("invalid array index");
  size_t n= 0;
  for (char c: s)
  {
    if (c < '0' || c > '9' || n > (std::numeric_limits<size_t>::max()-(c-'0'))/10)
      throw std::runtime_error("invalid array index");
    n= n*10 + c-'0';
  }
  return n;
}

Value *locate(Value &root, const std::vector<std::string> &path, size_t count)
{
  Value *v= &root;
  for (size_t i= 0; i < count; ++i)
  {
    if (v->type == Value::OBJECT)
    {
      auto j= v->object.find(path[i]);
      if (j == v->object.end()) throw std::runtime_error("path does not exist");
      v= &j->second;
    }
    else if (v->type == Value::ARRAY)
    {
      size_t n= index_of(path[i], v->array.size(), false);
      if (n >= v->array.size()) throw std::runtime_error("array index out of bounds");
      v= &v->array[n];
    }
    else throw std::runtime_error("path traverses a scalar");
  }
  return v;
}

Value remove_at(Value &root, const std::vector<std::string> &path)
{
  if (path.empty()) { Value old= root; root= Value(); return old; }
  Value *parent= locate(root, path, path.size()-1);
  if (parent->type == Value::OBJECT)
  {
    auto i= parent->object.find(path.back());
    if (i == parent->object.end()) throw std::runtime_error("remove path does not exist");
    Value old= i->second; parent->object.erase(i); return old;
  }
  if (parent->type == Value::ARRAY)
  {
    size_t n= index_of(path.back(), parent->array.size(), false);
    if (n >= parent->array.size()) throw std::runtime_error("remove index out of bounds");
    Value old= parent->array[n]; parent->array.erase(parent->array.begin()+n); return old;
  }
  throw std::runtime_error("remove parent is a scalar");
}

void add_at(Value &root, const std::vector<std::string> &path, const Value &value)
{
  if (path.empty()) { root= value; return; }
  Value *parent= locate(root, path, path.size()-1);
  if (parent->type == Value::OBJECT) parent->object[path.back()]= value;
  else if (parent->type == Value::ARRAY)
  {
    size_t n= index_of(path.back(), parent->array.size(), true);
    if (n > parent->array.size()) throw std::runtime_error("add index out of bounds");
    parent->array.insert(parent->array.begin()+n, value);
  }
  else throw std::runtime_error("add parent is a scalar");
}

const Value &member(const Value &op, const char *name, Value::Type type)
{
  auto i= op.object.find(name);
  if (i == op.object.end() || i->second.type != type)
    throw std::runtime_error(std::string("operation requires '")+name+"'");
  return i->second;
}

const Value &member(const Value &op, const char *name)
{
  auto i= op.object.find(name);
  if (i == op.object.end())
    throw std::runtime_error(std::string("operation requires '")+name+"'");
  return i->second;
}

void operation(Value &doc, const Value &entry)
{
  if (entry.type != Value::OBJECT) throw std::runtime_error("patch entry is not an object");
  const std::string &op= member(entry, "op", Value::STRING).scalar;
  std::vector<std::string> path= pointer(member(entry, "path", Value::STRING).scalar);
  if (op == "add") add_at(doc, path, member(entry, "value"));
  else if (op == "remove") (void) remove_at(doc, path);
  else if (op == "replace")
  {
    const Value value= member(entry, "value");
    if (!path.empty()) (void) locate(doc, path, path.size());
    (void) remove_at(doc, path); add_at(doc, path, value);
  }
  else if (op == "copy" || op == "move")
  {
    std::vector<std::string> from= pointer(member(entry, "from", Value::STRING).scalar);
    if (op == "move" && path.size() > from.size() &&
        std::equal(from.begin(), from.end(), path.begin()))
      throw std::runtime_error("move destination is inside source");
    Value value= *locate(doc, from, from.size());
    if (op == "move") (void) remove_at(doc, from);
    add_at(doc, path, value);
  }
  else if (op == "test")
  {
    const Value &value= member(entry, "value");
    if (!equal(*locate(doc, path, path.size()), value))
      throw std::runtime_error("test operation failed");
  }
  else throw std::runtime_error("unknown patch operation");
}

Value patch_entry(const char *op, const std::string &path, const Value *value)
{
  Value v; v.type= Value::OBJECT;
  v.object["op"]= Value::string(op); v.object["path"]= Value::string(path);
  if (value) v.object["value"]= *value;
  return v;
}

void make_diff(const Value &a, const Value &b, const std::string &path, Value &patch)
{
  if (equal(a, b)) return;
  if (a.type == Value::OBJECT && b.type == Value::OBJECT)
  {
    for (const auto &i: a.object)
      if (!b.object.count(i.first))
        patch.array.push_back(patch_entry("remove", path+"/"+token_escape(i.first), nullptr));
    for (const auto &i: b.object)
    {
      auto j= a.object.find(i.first);
      std::string child= path+"/"+token_escape(i.first);
      if (j == a.object.end()) patch.array.push_back(patch_entry("add", child, &i.second));
      else make_diff(j->second, i.second, child, patch);
    }
    return;
  }
  if (a.type == Value::ARRAY && b.type == Value::ARRAY)
  {
    size_t common= std::min(a.array.size(), b.array.size());
    for (size_t i= 0; i < common; ++i)
      make_diff(a.array[i], b.array[i], path+"/"+std::to_string(i), patch);
    for (size_t i= a.array.size(); i > b.array.size(); --i)
      patch.array.push_back(patch_entry("remove", path+"/"+std::to_string(i-1), nullptr));
    for (size_t i= common; i < b.array.size(); ++i)
      patch.array.push_back(patch_entry("add", path+"/-", &b.array[i]));
    return;
  }
  patch.array.push_back(patch_entry("replace", path, &b));
}

template <class F>
bool guarded(std::string *result, std::string *error, F fn)
{
  try { Value value= fn(); result->clear(); serialize(*result, value); error->clear(); return true; }
  catch (const std::exception &e) { result->clear(); *error= e.what(); return false; }
}
} // anonymous namespace

bool diff(const std::string &source, const std::string &target,
          std::string *result, std::string *error)
{
  return guarded(result, error, [&]() {
    Value a= Parser(source).parse(), b= Parser(target).parse(), out;
    out.type= Value::ARRAY; make_diff(a, b, "", out); return out;
  });
}

bool patch(const std::string &document, const std::string &patch_text,
           std::string *result, std::string *error)
{
  return guarded(result, error, [&]() {
    Value doc= Parser(document).parse(), ops= Parser(patch_text).parse();
    if (ops.type != Value::ARRAY) throw std::runtime_error("JSON Patch must be an array");
    for (const Value &op: ops.array) operation(doc, op);
    return doc;
  });
}
} // namespace json_extra
