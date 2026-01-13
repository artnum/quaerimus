#ifndef QUAERIMUS_H__
#define QUAERIMUS_H__
#include "array.h"
#include <memarena.h>
#include <mysql/mysql.h>
#include <stdint.h>
#include <stdio.h>
#define QURY_PARAMS_INIT_SIZE 40

#define quryptr_t uint64_t

#define QURY_DOUBLE(v)                                                         \
  ((union {                                                                    \
     double d;                                                                 \
     quryptr_t q;                                                              \
   }){.d = (v)}                                                                \
       .q)

#define QURY_None 0x0000 /* no bind */
#define QURY_Integer 0x0001
#define QURY_Float 0x0002
#define QURY_CString 0x0004
#define QURY_OString 0x0008
#define QURY_Bool 0x0010
#define QURY_Null 0x0020
#define QURY_DataCallback 0x1000
typedef uint16_t qury_bind_value_type_t;
typedef uint16_t qury_bind_result_type_t;

typedef size_t (*qury_data_callback)(uint8_t *buffer, size_t length);

typedef struct {
  MYSQL mysql;
} qury_conn_t;

typedef union {
  uint64_t i;
  double f;
  char *cstr;
  struct {
    uint8_t *ptr;
    size_t len;
  } ostr;
  bool b;
  qury_data_callback cb;
} qury_bind_value_t;

typedef struct {
  char *name;
  char *org_name;
  char *table;
  enum enum_field_types type;
  unsigned int charsetnr;
  unsigned int decimals;
  unsigned int flags;
} qury_field_name_t;

typedef struct {
  char *name;
  qury_bind_value_type_t type;
  qury_bind_value_t value;
  bool is_null;
  bool is_unsigned;
  char error;
  size_t length;
} qury_bind_t;

typedef struct {
  qury_conn_t conn;
  MYSQL_STMT *stmt;
  char *query;
  size_t query_length;

  /* bingings */
  MYSQL_BIND *binds;
  int field_cnt;
  array_t params;

  MYSQL_BIND *results;
  array_t fields;
  array_t values;

  bool result_bounded;
  bool params_bounded;

  /* internal use */
  mem_arena_t *arena;         /* arena for the stmt duration */
  mem_arena_t *query_arena;   /* arena for one query duration */
  mem_arena_t *results_arena; /* arena for one result set duration */
} qury_stmt_t;

void qury_conn_init(qury_conn_t *c);
qury_stmt_t *qury_new(qury_conn_t *conn);
void qury_stmt_free(qury_stmt_t *stmt);
bool qury_prepare(qury_stmt_t *stmt, const char *query, size_t length);
void qury_stmt_bind(qury_stmt_t *stmt, const char *name, quryptr_t ptr,
                    size_t vlen, qury_bind_value_type_t type);
/**
 * Execute a prepared statement
 */
bool qury_execute(qury_stmt_t *stmt);
/**
 * Fetch a result row
 */
bool qury_fetch(qury_stmt_t *stmt);

#define qury_stmt_bind_int(stmt, name, value)                                  \
  qury_stmt_bind((stmt), (name), (quryptr_t)(value), 0, QURY_Integer)
#define qury_stmt_bind_float(stmt, name, value)                                \
  qury_stmt_bind((stmt), (name), QURY_DOUBLE((value)), 0, QURY_Float)
#define qury_stmt_bind_bool(stmt, name, value)                                 \
  qury_stmt_bind((stmt), (name), (quryptr_t)(value), 0, QURY_Bool)
#define qury_stmt_bind_str(stmt, name, value)                                  \
  qury_stmt_bind((stmt), (name), (quryptr_t)(value), 0, QURY_CString)
#define qury_stmt_bind_bytes(stmt, name, value, len)                           \
  qury_stmt_bind((stmt), (name), (quryptr_t)(value), (len), QURY_OString)
#define qury_stmt_bind_lstr(stmt, name, callback)                              \
  qury_stmt_bind((stmt), (name), (quryptr_t)(callback), 0,                     \
                 QURY_CString | QURY_DataCallback)
#define qury_stmt_bind_lbytes(stmt, name, callback)                            \
  qury_stmt_bind((stmt), (name), (quryptr_t)(callback), 0,                     \
                 QURY_OString | QURY_DataCallback)

/**
 * Dump the statement to the specified file
 *
 * \param fp   File to dump to
 * \param stmt Statement to dump
 */
void qury_stmt_dump(FILE *fp, qury_stmt_t *stmt);
/**
 * Reset a statement before running again
 */
void qury_stmt_reset(qury_stmt_t *stmt);

/**
 * Get a field value
 */
qury_bind_t *qury_get_field_value(qury_stmt_t *stmt, const char *name);
#endif /* QUAERIMUS_H__ */
