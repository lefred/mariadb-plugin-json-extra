# mariadb-plugin-json-extra

`json_extra` is a MariaDB function plugin implementing JSON Patch
([RFC 6902](https://www.rfc-editor.org/rfc/rfc6902)) and JSON Pointer
([RFC 6901](https://www.rfc-editor.org/rfc/rfc6901)).

It provides two native SQL functions:

* `JSON_DIFF(source, target)` returns a deterministic RFC 6902 patch which
  transforms `source` into `target`.
* `JSON_PATCH(document, patch)` applies all six RFC operations: `add`,
  `remove`, `replace`, `move`, `copy`, and `test`.

Both arguments must be JSON strings. SQL `NULL` propagates to `NULL`; invalid
JSON, an invalid patch, a missing path, or a failed `test` also returns `NULL`.
Objects are serialized with keys in lexical order. JSON number comparison for
the `test` operation is numeric, so `1` and `1.0` compare equal.

## Build and install

Place this directory at `plugin/json_extra` in a MariaDB source tree, configure
and build MariaDB normally, then install the plugin:

```sql
INSTALL SONAME 'json_extra';
```

The plugin consists of two function-plugin descriptors in the same shared
object, so installing the library registers both functions. Uninstall it with:

```sql
UNINSTALL SONAME 'json_extra';
```

For a focused unit test build:

```sh
cmake --build <build-directory> --target json_extra_test
ctest --test-dir <build-directory> -R '^json_extra$'
```

## Examples

### JSON_DIFF: compute a patch between two documents

```sql
SELECT JSON_DIFF('{"a":1}', '{"a":2,"b":true}');
-- [{"op":"replace","path":"/a","value":2},{"op":"add","path":"/b","value":true}]
```

Removed keys produce `remove` operations, and array diffs are computed
element-by-element with trailing insertions appended (`path":"/-"`):

```sql
SELECT JSON_DIFF('{"a":1,"c":3}', '{"a":1}');
-- [{"op":"remove","path":"/c"}]

SELECT JSON_DIFF('[1,2]', '[1,2,3]');
-- [{"op":"add","path":"/-","value":3}]
```

A common workflow is to store `JSON_DIFF()` output as an audit/changelog
entry, then replay it later with `JSON_PATCH()`:

```sql
CREATE TABLE widget (id INT PRIMARY KEY, doc JSON);
INSERT INTO widget VALUES (1, '{"name":"gizmo","stock":10}');

-- capture what changed before an UPDATE
SELECT JSON_DIFF(doc, '{"name":"gizmo","stock":7}') FROM widget WHERE id = 1;
-- [{"op":"replace","path":"/stock","value":7}]
```

### JSON_PATCH: apply RFC 6902 operations

```sql
-- replace
SELECT JSON_PATCH('{"a":1}', '[{"op":"replace","path":"/a","value":2}]');
-- {"a":2}

-- add into an array at a given index
SELECT JSON_PATCH('["a","b"]', '[{"op":"add","path":"/1","value":"x"}]');
-- ["a","x","b"]

-- append to the end of an array with "-"
SELECT JSON_PATCH('["a","b"]', '[{"op":"add","path":"/-","value":"c"}]');
-- ["a","b","c"]

-- remove
SELECT JSON_PATCH('{"a":1,"b":2}', '[{"op":"remove","path":"/b"}]');
-- {"a":1}

-- move a value from one path to another
SELECT JSON_PATCH('{"a":1,"b":null}',
                  '[{"op":"move","from":"/a","path":"/b"}]');
-- {"b":1}

-- copy a value without removing the source
SELECT JSON_PATCH('{"a":1,"b":null}',
                  '[{"op":"copy","from":"/a","path":"/b"}]');
-- {"a":1,"b":1}

-- test: apply only if the assertion holds, otherwise return NULL
SELECT JSON_PATCH('{"a":1}', '[{"op":"test","path":"/a","value":1},
                              {"op":"replace","path":"/a","value":2}]');
-- {"a":2}

SELECT JSON_PATCH('{"a":1}', '[{"op":"test","path":"/a","value":2}]');
-- NULL  (the test failed)
```

`~0` and `~1` escape `~` and `/` inside a JSON Pointer token, so keys that
themselves contain a slash or tilde are still addressable:

```sql
SELECT JSON_PATCH('{"a/b":{"~x":1}}',
                  '[{"op":"replace","path":"/a~1b/~0x","value":2}]');
-- {"a/b":{"~x":2}}
```

Multiple operations run as a single all-or-nothing batch: if any operation
in the array fails (bad path, failed `test`, type mismatch), the whole call
returns `NULL` rather than a partially patched document.

```sql
SELECT JSON_PATCH('{"a":1}',
                  '[{"op":"replace","path":"/a","value":2},
                    {"op":"remove","path":"/missing"}]');
-- NULL
```

## Comparison with MySQL and PostgreSQL

Neither MySQL nor PostgreSQL ship a built-in implementation of RFC 6902
JSON Patch or a diff function that generates one; both only offer
RFC 7396 JSON Merge Patch semantics (whole-document merge, no `move`,
`copy`, `test`, or array-index addressing) plus a set of path-based
mutator functions. The table below maps this plugin's functions to the
closest native equivalents.

| Capability | `json_extra` (MariaDB) | MySQL | PostgreSQL |
|---|---|---|---|
| Generate an RFC 6902 patch between two documents | `JSON_DIFF(source, target)` | Not available | Not available |
| Apply an RFC 6902 patch (`add`/`remove`/`replace`/`move`/`copy`/`test`) | `JSON_PATCH(document, patch)` | Not available | Not available |
| [RFC 7396](https://www.rfc-editor.org/rfc/rfc7396) JSON Merge Patch (recursive merge, `null` deletes a key) | Not part of this plugin — MariaDB already has `JSON_MERGE_PATCH(target, patch)` natively (server built-in, since [MDEV-13992](https://jira.mariadb.org/browse/MDEV-13992)) | `JSON_MERGE_PATCH(target, patch)` | No dedicated function; approximated with `target \|\| patch` on `jsonb`, but `\|\|` only merges top-level keys and cannot delete a key with `null` |
| Set/replace a value at a path | via `JSON_PATCH` `replace`/`add` | `JSON_SET(doc, path, val)`, `JSON_REPLACE(doc, path, val)` | `jsonb_set(target, path, new_value)` |
| Insert only if the path doesn't already exist | via `JSON_PATCH` `add` (object) semantics differ: always overwrites | `JSON_INSERT(doc, path, val)` | `jsonb_set(target, path, new_value, false)` (`create_missing := false` for the inverse) |
| Remove a value at a path | via `JSON_PATCH` `remove` | `JSON_REMOVE(doc, path)` | `target #- path` operator, or `jsonb_delete` |
| Conditional apply / assert a value before mutating | `JSON_PATCH` `test` operation, whole batch fails on mismatch | Not available; must `SELECT` and check in application code | Not available; must `SELECT` and check in application code |
| Move or rename a member | `JSON_PATCH` `move` operation | Not available directly; simulate with `JSON_SET` + `JSON_REMOVE` in two steps (non-atomic) | Not available directly; simulate with `jsonb_set` + `#-` in two steps (non-atomic) |
| Copy a value to another path | `JSON_PATCH` `copy` operation | Not available directly; simulate with `JSON_SET` reading the source path | Not available directly; simulate with `jsonb_set` reading the source path |
| Standard this follows | RFC 6901 (Pointer) + RFC 6902 (Patch) | Own `path` expression syntax (not RFC 6901) | Own `path` array/operator syntax (not RFC 6901) |

In short: MySQL and PostgreSQL let you *mutate* a JSON document at one or
more explicit paths in a single call, and MySQL additionally supports
whole-document merge patching (RFC 7396). What neither offers natively is
this plugin's contribution: computing a *diff* between two documents
as a portable, storable RFC 6902 patch (`JSON_DIFF`), and applying such a
patch atomically with `move`, `copy`, and `test` semantics (`JSON_PATCH`).
That combination is normally only available through client-side
libraries (e.g. `jsonpatch` in Python, `fast-json-patch` in JavaScript) or
extensions, not as native SQL functions.
