
#include "include/quaerimus.h"
#include "include/array.h"
#include <assert.h>
#include <mariadb/mysql.h>
#include <memarena.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define _ST_NONE 0
#define _ST_QUOTE (1 << 4)   // 0b00001000
#define _ST_ESCAPE (1 << 3)  // 0b00000100
#define _ST_VARNAME (1 << 8) // 0b10000000
#define _ST_FAILED 0xFF      // 0b11111111

#define _st_set(s, v) ((s) |= (v))
#define _st_clear(s, v) ((s) &= ~(v))
#define _st_zero(s) ((s) = 0)
#define _st_isset(s, v) ((s) & (v))

#define _prefix_char ':'
/* allows variable name to be :varname: */
#define _is_quote_char(c) ((c) == '\'' || (c) == '`' || (c) == '"')
#define _is_separator_char(c)                                                  \
  ((c) == ' ' || (c) == '\t' || (c) == _prefix_char || (c) == ',' || (c) == ';')
#define _is_variable_prefix(c) ((c) == _prefix_char)
#define _is_variable_char(c)                                                   \
  (((c) >= '0' && (c) <= '9') || ((c) >= 'a' && (c) <= 'z') ||                 \
   ((c) >= 'A' && (c) <= 'Z') || ((c) == '_'))
#define _is_escape_char(c) ((c) == '\\')

static const char *_type_to_str(qury_bind_value_type_t type) {
  switch (type) {
  default:
  case QURY_None:
    return "none";
  case QURY_Integer:
    return "integer";
  case QURY_OString:
    return "bytes";
  case QURY_CString:
    return "string";
  case QURY_Bool:
    return "boolean";
  case QURY_Float:
    return "float";
  case QURY_Null:
    return "null";
  }
}

static void _dump_ostr(FILE *fp, uint8_t *ptr, size_t l) {
  for (size_t i = 0; i < l; i++) {
    fprintf(fp, "%02X ", ptr[i]);
  }
}
void qury_stmt_dump(FILE *fp, qury_stmt_t *stmt) {
  assert(stmt != NULL);
  int count_qm = 0;
  for (size_t i = 0; i < stmt->query_length; i++) {
    if (stmt->query[i] == '?') {
      count_qm++;
    }
  }
  fprintf(fp, "PARSED QUERY [%.*s]\n", (int)stmt->query_length, stmt->query);
  fprintf(fp, "\tPARAMETERS [expected %3d, found %3d]:\n", count_qm,
          (int)array_size(&stmt->params));
  for (size_t i = 0; i < array_size(&stmt->params); i++) {
    qury_bind_t *p = (qury_bind_t *)array_get(&stmt->params, i);
    fprintf(fp, "\t• %3ld %7s(%2d)\t%-20s ", i + 1, _type_to_str(p->type),
            p->type, p->name);
    switch (p->type) {
    case QURY_CString: {
      fprintf(fp, "\"%s\"", p->value.cstr);
    } break;
    case QURY_Float: {
      fprintf(fp, "\"%f\"", p->value.f);
    } break;
    case QURY_Integer: {
      fprintf(fp, "\"%ld\"", p->value.i);
    } break;
    case QURY_Bool: {
      fprintf(fp, "\"%s\"", p->value.b ? "TRUE" : "FALSE");
    } break;
    case QURY_OString: {
      _dump_ostr(fp, p->value.ostr.ptr, p->value.ostr.len);
    } break;

    default:
    case QURY_Null:
      fprintf(fp, "\"NULL\"");
      break;
    }

    fprintf(fp, "\n");
  }
}

static uint16_t _mtype_to_qurytype(enum enum_field_types type,
                                   unsigned int charsetnr) {
  switch (type) {
  case MYSQL_TYPE_VARCHAR:
    /* https://dev.mysql.com/doc/c-api/5.7/en/c-api-data-structures.html */
    if (charsetnr == 63) {
      return QURY_OString;
    }
    return QURY_CString;
  case MYSQL_TYPE_BIT:
    return QURY_Integer;
  case MYSQL_TYPE_DATE:
    return QURY_CString;
  case MYSQL_TYPE_BLOB:
    return QURY_OString;
  case MYSQL_TYPE_DATETIME:
    return QURY_CString;
  case MYSQL_TYPE_DATETIME2:
    return QURY_CString;
  case MYSQL_TYPE_LONGLONG:
    return QURY_Integer;
  case MYSQL_TYPE_DECIMAL:
    return QURY_Float;
  case MYSQL_TYPE_NEWDECIMAL:
    return QURY_Float;
  case MYSQL_TYPE_YEAR:
    return QURY_Integer;
  case MYSQL_TYPE_DOUBLE:
    return QURY_Float;
  case MYSQL_TYPE_ENUM:
    return QURY_CString;
  case MYSQL_TYPE_FLOAT:
    return QURY_Float;
  case MYSQL_TYPE_GEOMETRY:
    return QURY_CString;
  case MYSQL_TYPE_INT24:
    return QURY_Float;
  case MYSQL_TYPE_JSON:
    return QURY_CString;
  case MYSQL_TYPE_LONG:
    return QURY_Float;
  case MYSQL_TYPE_LONG_BLOB:
    return QURY_OString;
  case MYSQL_TYPE_MEDIUM_BLOB:
    return QURY_OString;
  case MYSQL_TYPE_TINY_BLOB:
    return QURY_OString;
  case MYSQL_TYPE_TINY:
    return QURY_Integer;
  case MYSQL_TYPE_NEWDATE:
    return QURY_CString;
  case MYSQL_TYPE_NULL:
    return QURY_Null;
  case MYSQL_TYPE_SET:
    return QURY_CString;
  case MYSQL_TYPE_SHORT:
    return QURY_Integer;
  case MYSQL_TYPE_STRING:
    /* https://dev.mysql.com/doc/c-api/5.7/en/c-api-data-structures.html */
    if (charsetnr == 63) {
      return QURY_OString;
    }
    return QURY_CString;
  case MYSQL_TYPE_VAR_STRING:
    /* https://dev.mysql.com/doc/c-api/5.7/en/c-api-data-structures.html */
    if (charsetnr == 63) {
      return QURY_OString;
    }
    return QURY_CString;
  case MYSQL_TYPE_TIME:
    return QURY_CString;
  case MYSQL_TYPE_TIME2:
    return QURY_CString;
  case MYSQL_TYPE_TIMESTAMP:
    return QURY_CString;
  case MYSQL_TYPE_TIMESTAMP2:
    return QURY_CString;
  default:
    return QURY_Null;
  }

  return QURY_Null;
}

static inline size_t _qury_process_param(qury_stmt_t *stmt, char *query,
                                         size_t *length) {
  int state = _ST_NONE;
  size_t name_start = 0;
  size_t removed_size = 0;
  size_t i = 0;
  for (i = 0; i < *length; i++) {
    /* previous was escape char, so we skip this one as \:varname is not allowed
     */
    if (_st_isset(state, _ST_ESCAPE)) {
      _st_clear(state, _ST_ESCAPE);
      continue;
    }
    /*  escape char */
    if (_is_escape_char(query[i])) {
      _st_set(state, _ST_ESCAPE);
      continue;
    }

    /* quotes */
    if (_is_quote_char(query[i])) {
      if (_st_isset(state, _ST_QUOTE)) {
        _st_clear(state, _ST_QUOTE);
      } else if (!_st_isset(state, _ST_QUOTE)) {
        _st_set(state, _ST_QUOTE);
      }
      continue;
    }
    /* within a quote we don't care */
    if (_st_isset(state, _ST_QUOTE)) {
      continue;
    }

    if (_is_separator_char(query[i]) && _st_isset(state, _ST_VARNAME)) {
      removed_size = i - name_start;
      query[name_start - 1] = '?';
      qury_bind_t *bind = mem_alloc(stmt->arena, sizeof(*bind));
      if (!bind) {
        /* TODO : error handling */
      }
      bind->name = mem_strndup(stmt->arena, &query[name_start], i - name_start);
      array_push(&stmt->params, (uintptr_t)bind);
      memmove(&query[name_start], &query[i],
              *length - (name_start + removed_size));
      *length -= removed_size;
      i -= removed_size;
      _st_clear(state, _ST_VARNAME);
      name_start = 0;
      continue;
    }

    if (!_is_variable_char(query[i]) && _st_isset(state, _ST_VARNAME)) {
      _st_clear(state, _ST_VARNAME);
      continue;
    }

    if (_is_variable_prefix(query[i])) {
      _st_set(state, _ST_VARNAME);
      name_start = i + 1;
      continue;
    }
  }
  /* reach the end of string within a variable name, so it's a variable */
  if (state != _ST_FAILED && _st_isset(state, _ST_VARNAME)) {
    removed_size = i - name_start;
    query[name_start - 1] = '?';
    qury_bind_t *bind = mem_alloc(stmt->arena, sizeof(*bind));
    if (!bind) {
      /* TODO : error handling */
    }
    bind->name = mem_strndup(stmt->arena, &query[name_start], i - name_start);
    array_push(&stmt->params, (uintptr_t)bind);

    memmove(&query[name_start], &query[i],
            *length - (name_start + removed_size));
    *length -= removed_size;
    _st_clear(state, _ST_VARNAME);
  }

  if (state == _ST_FAILED) {
    /* TODO : something as we have an error */
  }

  stmt->binds = mem_alloc(stmt->arena,
                          sizeof(MYSQL_BIND) * (array_size(&stmt->params) + 1));
  if (!stmt->binds) {
    /* TODO : error to handle again */
  }

  return 0;
}

void qury_conn_init(qury_conn_t *c) {
  memset(c, 0, sizeof(*c));
  mysql_init(&c->mysql);
}

qury_stmt_t *qury_new(qury_conn_t *conn) {
  assert(conn != NULL);

  mem_arena_t *arena = mem_arena_new(sizeof(qury_stmt_t));
  if (!arena) {
    return NULL;
  }
  qury_stmt_t *stmt = mem_alloc(arena, sizeof(qury_stmt_t));
  if (!stmt) {
    mem_arena_destroy(arena);
    return NULL;
  }
  memset(stmt, 0, sizeof(qury_stmt_t));
  stmt->query_arena = mem_arena_new(0);
  if (!stmt->query_arena) {
    mem_arena_destroy(arena);
    return NULL;
  }
  stmt->results_arena = mem_arena_new(0);
  if (!stmt->results_arena) {
    mem_arena_destroy(stmt->query_arena);
    mem_arena_destroy(arena);
    return NULL;
  }
  stmt->arena = arena;
  memcpy(&stmt->conn, conn, sizeof(*conn));
  return stmt;
}

bool qury_prepare(qury_stmt_t *stmt, const char *query, size_t length) {
  assert(stmt != NULL);
  assert(query != NULL);
  if (length == 0) {
    length = strlen(query);
  }

  if (!array_init(&stmt->params, QURY_PARAMS_INIT_SIZE)) {
    return false;
  }
  stmt->query = mem_strndup(stmt->arena, query, length);
  if (!stmt->query) {
    return false;
  }
  stmt->query_length = length;

  _qury_process_param(stmt, stmt->query, &stmt->query_length);
  stmt->stmt = mysql_stmt_init(&stmt->conn.mysql);
  if (!stmt->stmt) {
    return false;
  }

  if (mysql_ping(&stmt->conn.mysql)) {
    fprintf(stderr, "mysql_ping : %s\n", mysql_error(&stmt->conn.mysql));
  }
  if (mysql_stmt_prepare(stmt->stmt, stmt->query, stmt->query_length)) {
    fprintf(stderr, "mysql_stmt_prepare : %s\n",
            mysql_error(&stmt->conn.mysql));
    return false;
  }
  return true;
}

void qury_stmt_free(qury_stmt_t *stmt) {
  if (stmt != NULL) {
    mysql_stmt_close(stmt->stmt);
    array_destroy(&stmt->params);
    array_destroy(&stmt->fields);
    mem_arena_destroy(stmt->query_arena);
    mem_arena_destroy(stmt->results_arena);
    mem_arena_destroy(stmt->arena);
  }
}

#define DATA_CALLBACK_BUFFER_SIZE 4096
bool qury_execute(qury_stmt_t *stmt) {
  assert(stmt != NULL);

  if (!stmt->params_bounded) {
    if (mysql_stmt_bind_param(stmt->stmt, stmt->binds)) {
      fprintf(stderr, "mysq_stmt_bind_param : %s\n",
              mysql_error(&stmt->conn.mysql));
    }
    stmt->params_bounded = true;
  }

  /* handle long data send */
  size_t index = 0;
  uintptr_t value = 0;
  array_foreach(&stmt->params, index, value) {
    qury_bind_t *param = (qury_bind_t *)value;
    uint8_t buffer[DATA_CALLBACK_BUFFER_SIZE] = {0};
    if (param->type & QURY_DataCallback) {
      size_t rlen = 0;
      while ((rlen = param->value.cb(buffer, DATA_CALLBACK_BUFFER_SIZE)) > 0) {
        if (!mysql_stmt_send_long_data(stmt->stmt, index, (const char *)buffer,
                                       rlen)) {
          break;
        }
        rlen = 0;
      }
    }
  }

  if (mysql_stmt_execute(stmt->stmt)) {
    fprintf(stderr, "mysql_stmt_execute: %s\n", mysql_stmt_error(stmt->stmt));
    return false;
  }

  if (!stmt->result_bounded) {
    MYSQL_RES *meta = mysql_stmt_result_metadata(stmt->stmt);
    MYSQL_FIELD *field = NULL;
    if (meta == NULL) {
      fprintf(stderr, "mysql_stmt_result_metadata : %s\n",
              mysql_error(&stmt->conn.mysql));
    }

    stmt->field_cnt = mysql_num_fields(meta);
    if (stmt->field_cnt > 0) {
      array_init(&stmt->fields, stmt->field_cnt);
      while ((field = mysql_fetch_field(meta)) != NULL) {
        qury_field_name_t *f =
            mem_alloc(stmt->results_arena, sizeof(qury_field_name_t));
        f->type = field->type;
        f->charsetnr = field->charsetnr;
        f->flags = field->flags;
        f->decimals = field->decimals;
        f->name =
            mem_strndup(stmt->results_arena, field->name, field->name_length);
        f->org_name = mem_strndup(stmt->results_arena, field->org_name,
                                  field->org_name_length);
        f->table =
            mem_strndup(stmt->results_arena, field->table, field->table_length);
        array_push(&stmt->fields, (uintptr_t)f);
      }
    }
    mysql_free_result(meta);
    stmt->result_bounded = true;
  }
  return true;
}

bool qury_fetch(qury_stmt_t *stmt) {
  if (!stmt->results) {
    stmt->results =
        mem_alloc(stmt->results_arena, sizeof(MYSQL_BIND) * stmt->field_cnt);
    if (!stmt->results) {
      return false;
    }
    if (!array_init(&stmt->values, stmt->field_cnt)) {
      return false;
    }

    for (int i = 0; i < stmt->field_cnt; i++) {
      qury_bind_t *mybind = mem_alloc(stmt->results_arena, sizeof(qury_bind_t));
      if (!mybind) {
        return false;
      }
      qury_field_name_t *field =
          (qury_field_name_t *)array_get(&stmt->fields, i);
      memset(&mybind->value, 0, sizeof(qury_bind_value_t));
      memset(&stmt->results[i], 0, sizeof(MYSQL_BIND));
      mybind->name = field->name;
      mybind->is_null = false;
      mybind->is_unsigned = false;
      mybind->length = 0;
      mybind->type = _mtype_to_qurytype(field->type, field->charsetnr);

      stmt->results[i].buffer_type = field->type;
      stmt->results[i].buffer_length = 0;
      stmt->results[i].buffer = NULL;
      stmt->results[i].error = &mybind->error;
      stmt->results[i].length = &mybind->length;
      switch (mybind->type) {
      case QURY_Null: {
        mybind->length = sizeof(uint64_t);
        mybind->is_null = true;
        stmt->results[i].buffer = &mybind->value.i;
        stmt->results[i].buffer_length = mybind->length;
      } break;
      case QURY_Bool: {
        mybind->length = sizeof(bool);
        stmt->results[i].buffer = &mybind->value.b;
        stmt->results[i].buffer_length = mybind->length;
      } break;
      case QURY_Integer: {
        mybind->length = sizeof(uint64_t);
        stmt->results[i].buffer = &mybind->value.i;
        stmt->results[i].buffer_length = mybind->length;
      } break;
      case QURY_Float: {
        mybind->length = sizeof(double);
        stmt->results[i].buffer = &mybind->value.f;
        stmt->results[i].buffer_length = mybind->length;
      } break;
      default: {
      } break;
      }
      array_push(&stmt->values, (uintptr_t)mybind);
    }
    mysql_stmt_bind_result(stmt->stmt, stmt->results);
  } else {
    /* nullify strings buffer so we get the size we need to allocated */
    for (int i = 0; i < stmt->field_cnt; i++) {
      qury_bind_t *mybind = ((qury_bind_t *)array_get(&stmt->values, i));
      memset(&mybind->value, 0, sizeof(qury_bind_value_t));
      switch (mybind->type) {
      case QURY_CString:
      case QURY_OString: {
        stmt->results[i].buffer = NULL;
        stmt->results[i].length_value = 0;
        stmt->results[i].buffer_length = 0;
      } break;
      }
    }
  }

  int status = mysql_stmt_fetch(stmt->stmt);
  if (status == 1 || status == MYSQL_NO_DATA)
    return false;

  for (int i = 0; i < stmt->field_cnt; i++) {
    qury_bind_t *mybind = ((qury_bind_t *)array_get(&stmt->values, i));
    switch (mybind->type) {
    case QURY_CString: {
      void *tmp = mem_realloc(stmt->results_arena, mybind->value.cstr,
                              sizeof(uint8_t) * (mybind->length + 1));
      if (!tmp) {
        break;
      }
      mybind->value.cstr = tmp;
      mybind->value.cstr[mybind->length] = '\0';
      stmt->results[i].buffer = mybind->value.cstr;
      stmt->results[i].buffer_length = mybind->length;
      if (mysql_stmt_fetch_column(stmt->stmt, &stmt->results[i], i, 0) != 0) {
        break;
      }
    } break;
    case QURY_OString: {
      void *tmp = mem_realloc(stmt->results_arena, mybind->value.ostr.ptr,
                              sizeof(uint8_t) * (mybind->length));
      if (!tmp) {
        break;
      }
      mybind->value.ostr.ptr = tmp;
      mybind->value.ostr.len = mybind->length;
      stmt->results[i].buffer = mybind->value.ostr.ptr;
      stmt->results[i].buffer_length = mybind->length;
      if (mysql_stmt_fetch_column(stmt->stmt, &stmt->results[i], i, 0) != 0) {
        break;
      }
    } break;
    default: {
      mybind->is_unsigned = !!stmt->results[i].is_unsigned;
      mybind->is_null =
          !!(stmt->results[i].is_null == 0 ? stmt->results[i].is_null_value
                                           : true);
    } break;
    }
  }

  return true;
}

void qury_stmt_bind(qury_stmt_t *stmt, const char *name, quryptr_t ptr,
                    size_t vlen, qury_bind_value_type_t type) {
  assert(stmt != NULL);
  assert(name != NULL);

  size_t index;
  uintptr_t value;
  mem_arena_t *arena = stmt->query_arena;
  /* param name can be used several time, so don't break out of the loop in
   * case one is found
   */
  array_foreach(&stmt->params, index, value) {
    qury_bind_t *param = (qury_bind_t *)value;
    if (strcasecmp(param->name, name) == 0) {
      memset(stmt->binds + index, 0, sizeof(*stmt->binds));
      MYSQL_BIND *mybind = stmt->binds + index;
      mybind->length = &param->length;
      mybind->error = &param->error;
      stmt->binds[index].is_null = 0;
      switch (type) {
      case QURY_Integer: {
        memcpy(&param->value.i, &ptr, sizeof(quryptr_t));
        param->length = sizeof(quryptr_t);
        mybind->buffer = &param->value.i;
        mybind->buffer_type = MYSQL_TYPE_LONGLONG;
      } break;
      case QURY_Bool: {
        /* char is big enough */
        param->value.b = !!ptr;
        param->length = sizeof(param->value.b);
        mybind->buffer = &param->value.b;
        mybind->buffer_type = MYSQL_TYPE_TINY;
      } break;
      case QURY_Float: {
        memcpy(&param->value.f, &ptr, sizeof(double));
        param->length = sizeof(double);
        mybind->buffer = &param->value.f;
        mybind->buffer_type = MYSQL_TYPE_DOUBLE;
      } break;
      case QURY_CString: {
        if ((uintptr_t)ptr != 0) {
          if (type & QURY_DataCallback) {
            param->value.cb = (qury_data_callback)ptr;
          } else {
            param->value.cstr = mem_strdup(arena, (const char *)(uintptr_t)ptr);
            param->length = strlen(param->value.cstr);
            mybind->buffer = &param->value.cstr;
          }
          mybind->buffer_type = MYSQL_TYPE_STRING;
          break;
        }
        goto set_param_null;
      } break;
      case QURY_OString: {
        if ((uintptr_t)ptr != 0 && vlen > 0) {
          if (type & QURY_DataCallback) {
            param->value.cb = (qury_data_callback)ptr;
          } else {
            param->value.ostr.ptr =
                mem_memdup(arena, (const uint8_t *)(uintptr_t)ptr, vlen);
            param->value.ostr.len = vlen;
            mybind->buffer = &param->value.ostr.ptr;
          }
          mybind->length = &param->value.ostr.len;
          mybind->buffer_type = MYSQL_TYPE_BLOB;
          break;
        }
        goto set_param_null;
      } break;

      default:
        type = QURY_Null;
        /* fall through */
      case QURY_Null: {
      set_param_null:
        /* no value for null */
        stmt->binds[index].is_null = (char *)&param->is_null;
        param->length = 0;
        mybind->buffer_type = MYSQL_TYPE_NULL;
        mybind->buffer = NULL;
      } break;
      }
      param->type = type;
    }
  }
}

qury_bind_t *qury_get_field_value(qury_stmt_t *stmt, const char *name) {
  size_t i = 0;
  for (i = 0; i < array_size(&stmt->fields); i++) {
    qury_field_name_t *field = (qury_field_name_t *)array_get(&stmt->fields, i);
    if (strcmp(name, field->name) == 0) {
      break;
    }
    if (strcmp(name, field->org_name) == 0) {
      break;
    }
  }
  if (i >= array_size(&stmt->fields)) {
    return NULL;
  }
  return (qury_bind_t *)array_get(&stmt->values, i);
}

void qury_stmt_reset(qury_stmt_t *stmt) { mysql_stmt_reset(stmt->stmt); }
