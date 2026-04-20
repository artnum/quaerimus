#ifndef QUAERIMUS_H__
#define QUAERIMUS_H__
#include "array.h"
#include "quaerimus_common.h"
#include <assert.h>
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
#define QURY_DateTime 0x0040
#define QURY_DataCallback 0x1000
typedef uint16_t qury_bind_value_type_t;
typedef uint16_t qury_bind_result_type_t;

typedef size_t (*qury_data_callback)(uint8_t *buffer, size_t length);

typedef struct {
  MYSQL *mysql;
  char *current_db;
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
  MYSQL_TIME dt;
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
  bool query_executed;

  /* internal use */
  void *allocator; /* arena for the stmt duration */
} qury_stmt_t;

#define qury_error(conn) mysql_error((conn)->mysql)

void qury_conn_init(qury_conn_t *c);

/**
 * \brief Set the allocator
 */
void qury_init(qury_allocator_t *allocator);

/**
 * \brief Close and clean database connection
 *
 * Will call mysql_close and free everything allocated (except statements
 * created with qury_new).
 *
 * \param [in] conn A \ref qury_conn_t pointer
 */
void qury_close(qury_conn_t *conn);

/**
 * \brief Initialize a new query
 *
 * Initialize a new query object. It can be used multiple time for different
 * query.
 *
 * \param [in] conn A \ref qury_conn_t pointer
 * \param [in] allocator_userptr The context of the allocator for this query
 * \return A new \ref qury_stmt_t object or NULL in case of failure
 */
qury_stmt_t *qury_new(qury_conn_t *conn, void *allocator_userptr);

/**
 * \brief Select current database
 *
 * Select what is the current database for the connection. The database is
 * saved in \ref qury_conn_t so when called multiple time with the same 
 * \a dbname, there is no round trip to the server. So if you modify externally
 * the current database, you should reset the state by calling the function
 * with \a dname set NULL.
 *
 * \param [in] conn A \ref qury_conn_t pointer
 * \param [in] dbname The database to select. Use NULL to reset the internal
 *                    database name, it will not unselect on the server, just
 *                    make sure that next call will touch the database server.
 * \return True for success, false otherwise.
 */
bool qury_select_db(qury_conn_t *conn, const char *dbname);

/**
 * \brief Reset statment
 *
 * Reset a statement so it can rebound and re-run.
 *
 * \param [in] stmt A prepared statement
 */
void qury_reset(qury_stmt_t *stmt);

/**
 * \brief Close and free
 *
 * Close the prepared statement and free all data associated with.
 *
 * \warning Not clear how I want to handle that ... still work in progress
 * \param [in] stmt A prepared statement
 */
void qury_free(qury_stmt_t *stmt);

/**
 * \brief Prepare a statement
 *
 * Prepare a statement with named parameters (in the form of ":name_param").
 *
 */

bool qury_prepare(qury_stmt_t *stmt, const char *query, size_t length);
/**
 * \brief Bind a parameter to a statement
 *
 * Bind a value to the named parmeter. The name must be without colon, if the
 * request is <em>SELECT * FROM t WHERE id = :id</em>, the \a name parameter
 * is <em>"id"</em>.
 *
 */
bool qury_stmt_bind(qury_stmt_t *stmt, const char *name, quryptr_t ptr,
                    size_t vlen, qury_bind_value_type_t type);
#define qury_stmt_free(stmt) qury_free(stmt)

/**
 * \brief Execute a prepared statement
 *
 * Execute a prepared statement, fetch columns from the result (without
 * fetching any data yet, you must call \ref qury_fetch).
 *
 * \param [in] stmt A prepared statement
 * \return True for success, false otherwise
 */
bool qury_execute(qury_stmt_t *stmt);

/**
 * \brief Fetch the next row
 *
 * Fetch the next row and keep it ready to get value with \ref qury_get_value.
 *
 * \param [in] stmt An execute prepared statement.
 * \return True while there is data, false otherwise
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
 * \brief Reset a statement before running again
 *
 * Reset a statement, basicaly just a wrapper around \a mysql_stmt_reset.
 *
 * \warning Not clear how I want to handle that ... still work in progress
 * \param [in] A prepared statement
 */
void qury_stmt_reset(qury_stmt_t *stmt);

/**
 * \brief Get a field value
 *
 * Get a column by name of the current row. This must be called after 
 * \ref qury_fetch and is valid until the next \ref qury_fetch. The value can 
 * be obtained by calling \a qury_get_[cstr|int|float|datetime].
 *
 * \param [in] stmt The SQL statement, \ref qury_fetch should have been called
 *                  before calling this function
 * \param [in] name Column name. Can be the renamed name (<em>SELECT x AS 
 *                  y</em> would be \a y) or the original name (\a x in the
 *                  previous example)
 * \return A pointer to the column of the current row.
 */
qury_bind_t *qury_get_field_value(qury_stmt_t *stmt, const char *name);

/**
 * \brief Get a field value
 *
 * Same as \ref qury_get_field_value. The column reference is passed as
 * parameter to simplify successive get. There is no reason to use
 * \ref qury_get_field_value instead of this one.
 * \param [in] stmt The SQL statement, \ref qury_fetch should have been called
 *                  before calling this function
 * \param [in] name Column name. Can be the renamed name (<em>SELECT x AS 
 *                  y</em> would be \a y) or the original name (\a x in the
 *                  previous example)
 * \param [out] v A pointer to the column of the current row. It will be set to
 *                NULL if the column doesn't exist or the value is NULL.
 * \return True if the value exists, false otherwise.
 */
static inline bool qury_get_value(qury_stmt_t *stmt, const char *name,
                                  qury_bind_t **v) {
  assert(v != NULL);

  /* reset v to null here as in usage we do a serie of call with the same v
   * without cleaning it up */
  *v = NULL;
  qury_bind_t *_v = qury_get_field_value(stmt, name);
  if (!_v || _v->type == QURY_Null || _v->is_null) {
    return false;
  }
  if (v) {
    *v = _v;
  }
  return true;
}

/**
 * \brief Return column value as c string
 *
 * Get the string value of the current column obtained by calling 
 * \ref qury_get_value.
 *
 * \param [in] v A pointer set by \ref qury_get_value. Can be NULL.
 * \return A pointer to a string, user should copy this value, or NULL in case
 *         v is NULL or database field is set to NULL.
 * \see qury_get_value
 */
static inline const char * qury_get_cstr(qury_bind_t *v) {
    if (!v || v->is_null) { return NULL; }
    return v->value.cstr;
}

/**
 * \brief Return column value as integer 
 *
 * Get the string value of the current column obtained by calling 
 * \ref qury_get_value. Integer value is unsigned 64 bits, it can be cast to
 * signed or smaller value if needed.
 *
 * \param [in] v A pointer set by \ref qury_get_value. Can be NULL.
 * \return An integer or 0 if NULL 
 * \see qury_get_value
 */
static inline uint64_t qury_get_int(qury_bind_t *v) {
    if (!v || v->is_null) { return 0; }
    return v->value.i;
}

/**
 * \brief Return column value as double
 *
 * Get the string value of the current column obtained by calling 
 * \ref qury_get_value.
 *
 * \param [in] v A pointer set by \ref qury_get_value. Can be NULL.
 * \return A double or 0.0 if null
 * \see qury_get_value
 */
static inline double qury_get_float(qury_bind_t *v) {
    if (!v || v->is_null) { return 0.0; }
    return v->value.f;
}

static inline bool qury_get_bool(qury_bind_t *v) {
    if (!v || v->is_null) { return false; }
    return v->value.b;
}

/**
 * \warning This is not the definitive form for this function, may change
 * \todo Finish the datetime support
 */
static inline MYSQL_TIME qury_get_datetime(qury_bind_t *v) {
    if (!v || v->is_null) { return (MYSQL_TIME){0}; }
    return v->value.dt;
}

static inline bool qury_is_null(qury_bind_t *v) {
    if (!v || v->is_null) { return true; }
    return false;
}

#endif /* QUAERIMUS_H__ */
