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


/* RFC 6902 JSON Patch functions for MariaDB. */
#define MYSQL_SERVER
#include "mariadb.h"
#include "sql_class.h"
#include "item.h"
#include "json_extra.h"
#include <mysql/plugin_function.h>

class Item_func_json_extra: public Item_str_func
{
  bool m_diff;
public:
  Item_func_json_extra(THD *thd, Item *a, Item *b, bool diff)
    : Item_str_func(thd, a, b), m_diff(diff) {}

  bool fix_length_and_dec(THD *) override
  {
    decimals= 0;
    fix_length_and_charset(MAX_BLOB_WIDTH, &my_charset_utf8mb4_bin);
    set_maybe_null();
    return false;
  }

  LEX_CSTRING func_name_cstring() const override
  {
    static const LEX_CSTRING names[]=
    {
      { STRING_WITH_LEN("json_patch") },
      { STRING_WITH_LEN("json_diff") }
    };
    return names[m_diff ? 1 : 0];
  }

  String *val_str(String *to) override
  {
    StringBuffer<STRING_BUFFER_USUAL_SIZE> left, right;
    String *a= args[0]->val_str(&left);
    String *b= args[1]->val_str(&right);
    if (!a || !b)
    {
      null_value= true;
      return nullptr;
    }

    std::string result, error;
    bool ok= m_diff
      ? json_extra::diff(std::string(a->ptr(), a->length()),
                         std::string(b->ptr(), b->length()), &result, &error)
      : json_extra::patch(std::string(a->ptr(), a->length()),
                          std::string(b->ptr(), b->length()), &result, &error);
    if (!ok || to->copy(result.data(), result.size(), &my_charset_utf8mb4_bin))
    {
      null_value= true;
      return nullptr;
    }
    null_value= false;
    return to;
  }

  Item *shallow_copy(THD *thd) const override
  { return get_item_copy<Item_func_json_extra>(thd, this); }
};

template <bool diff>
class Create_json_extra: public Create_func_arg2
{
public:
  Item *create_2_arg(THD *thd, Item *a, Item *b) override
  { return new (thd->mem_root) Item_func_json_extra(thd, a, b, diff); }
};

static Create_json_extra<true> create_json_diff;
static Create_json_extra<false> create_json_patch;
static Plugin_function descriptor_json_diff(&create_json_diff);
static Plugin_function descriptor_json_patch(&create_json_patch);

#define JSON_EXTRA_PLUGIN(descriptor, name, description) \
  { MariaDB_FUNCTION_PLUGIN, descriptor, name, "lefred", \
    description, PLUGIN_LICENSE_GPL, 0, 0, 0x0100, nullptr, nullptr, "0.2.0", \
    MariaDB_PLUGIN_MATURITY_BETA }

maria_declare_plugin(json_extra)
  JSON_EXTRA_PLUGIN(&descriptor_json_diff, "json_diff",
                    "Create an RFC 6902 JSON Patch"),
  JSON_EXTRA_PLUGIN(&descriptor_json_patch, "json_patch",
                    "Apply an RFC 6902 JSON Patch")
maria_declare_plugin_end;
