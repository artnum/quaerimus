/**
 * Integration tests for quaerimus (named params, bind, execute, fetch).
 *
 * Requires a live MariaDB/MySQL server. Connection is taken from the
 * environment (all optional except user when you want integration):
 *
 *   QURY_TEST_HOST     default: localhost
 *   QURY_TEST_USER     if unset, the whole suite is skipped
 *   QURY_TEST_PASSWORD default: empty
 *   QURY_TEST_DB       default: test (created if missing when privileges allow)
 *   QURY_TEST_PORT     default: 0 (library default)
 *
 * Example:
 *   QURY_TEST_USER=root QURY_TEST_PASSWORD=secret make test-qury
 */
#include "../src/include/quaerimus.h"
#include "test_util.h"
#include <check.h>
#include <mariadb/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *env_or(const char *key, const char *fallback) {
  const char *v = getenv(key);
  return (v && v[0]) ? v : fallback;
}

static int integration_enabled(void) {
  const char *user = getenv("QURY_TEST_USER");
  return user && user[0];
}

static qury_conn_t g_conn;
static int g_connected;

static void setup_conn(void) {
  g_connected = 0;
  if (!integration_enabled()) {
    return;
  }

  qury_init(&test_malloc_allocator);

  if (mysql_library_init(0, NULL, NULL)) {
    ck_abort_msg("mysql_library_init failed");
  }
  if (!qury_conn_init(&g_conn)) {
    ck_abort_msg("qury_conn_init failed");
  }

  const char *host = env_or("QURY_TEST_HOST", "localhost");
  const char *user = env_or("QURY_TEST_USER", "");
  const char *pass = env_or("QURY_TEST_PASSWORD", "");
  const char *db = env_or("QURY_TEST_DB", "test");
  unsigned int port = (unsigned int)atoi(env_or("QURY_TEST_PORT", "0"));

  if (!mysql_real_connect(g_conn.mysql, host, user, pass, NULL, port, NULL,
                          0)) {
    ck_abort_msg("mysql_real_connect failed: %s", mysql_error(g_conn.mysql));
  }

  char q[256];
  snprintf(q, sizeof(q), "CREATE DATABASE IF NOT EXISTS `%s`", db);
  if (mysql_query(g_conn.mysql, q)) {
    ck_abort_msg("CREATE DATABASE failed: %s", mysql_error(g_conn.mysql));
  }
  if (!qury_select_db(&g_conn, db)) {
    ck_abort_msg("qury_select_db failed: %s", qury_error(&g_conn));
  }

  /* Isolated fixture table */
  if (mysql_query(g_conn.mysql, "DROP TABLE IF EXISTS qury_test_rows")) {
    ck_abort_msg("DROP TABLE failed: %s", mysql_error(g_conn.mysql));
  }
  if (mysql_query(g_conn.mysql,
                  "CREATE TABLE qury_test_rows ("
                  "  id INT NOT NULL,"
                  "  name VARCHAR(64) NOT NULL,"
                  "  flag TINYINT(1) NOT NULL,"
                  "  price DOUBLE NOT NULL,"
                  "  note VARCHAR(64) NULL"
                  ") ENGINE=InnoDB")) {
    ck_abort_msg("CREATE TABLE failed: %s", mysql_error(g_conn.mysql));
  }
  if (mysql_query(g_conn.mysql,
                  "INSERT INTO qury_test_rows (id, name, flag, price, note) VALUES "
                  "(1, 'alpha', 1, 1.5, 'hello'),"
                  "(2, 'beta', 0, 2.25, ''),"
                  "(3, 'gamma', 1, 3.0, NULL)")) {
    ck_abort_msg("INSERT failed: %s", mysql_error(g_conn.mysql));
  }

  g_connected = 1;
}

static void teardown_conn(void) {
  if (!g_connected) {
    return;
  }
  mysql_query(g_conn.mysql, "DROP TABLE IF EXISTS qury_test_rows");
  qury_close(&g_conn);
  mysql_library_end();
  g_connected = 0;
}

static void require_db(void) {
  if (!integration_enabled()) {
    ck_abort_msg("skipped: set QURY_TEST_USER to run integration tests");
  }
  if (!g_connected) {
    ck_abort_msg("not connected");
  }
}

START_TEST(test_named_param_rewrite) {
  require_db();

  qury_stmt_t *stmt = qury_new(&g_conn, NULL);
  ck_assert_ptr_nonnull(stmt);

  ck_assert(qury_prepare(
      stmt, "SELECT id, name FROM qury_test_rows WHERE id = :id AND name = :name",
      0));

  ck_assert_ptr_nonnull(stmt->query);
  /* placeholders rewritten to ? */
  ck_assert(strchr(stmt->query, '?') != NULL);
  ck_assert(strstr(stmt->query, ":id") == NULL);
  ck_assert(strstr(stmt->query, ":name") == NULL);
  ck_assert_uint_eq(array_size(&stmt->params), 2);

  qury_bind_t *p0 = (qury_bind_t *)array_get(&stmt->params, 0);
  qury_bind_t *p1 = (qury_bind_t *)array_get(&stmt->params, 1);
  ck_assert_str_eq(p0->name, "id");
  ck_assert_str_eq(p1->name, "name");

  qury_free(stmt);
}
END_TEST

START_TEST(test_named_param_edge_forms) {
  require_db();

  qury_stmt_t *stmt = qury_new(&g_conn, NULL);
  ck_assert_ptr_nonnull(stmt);

  /* terminator is ')', '+', newline — all should still emit params */
  ck_assert(qury_prepare(stmt,
                         "SELECT id FROM qury_test_rows WHERE id IN (:a) OR id = "
                         ":b+0 OR id = :c\n",
                         0));
  ck_assert_uint_eq(array_size(&stmt->params), 3);
  ck_assert(strstr(stmt->query, ":a") == NULL);
  ck_assert(strstr(stmt->query, ":b") == NULL);
  ck_assert(strstr(stmt->query, ":c") == NULL);

  qury_free(stmt);
}
END_TEST

START_TEST(test_bind_execute_fetch_types) {
  require_db();

  qury_stmt_t *stmt = qury_new(&g_conn, NULL);
  ck_assert_ptr_nonnull(stmt);

  ck_assert(qury_prepare(stmt,
                         "SELECT id, name, flag, price, note FROM qury_test_rows "
                         "WHERE id = :id",
                         0));
  ck_assert(qury_stmt_bind_int(stmt, "id", 1));
  ck_assert(qury_execute(stmt));

  ck_assert(qury_fetch(stmt));

  qury_bind_t *v = NULL;
  ck_assert(qury_get_value(stmt, "id", &v));
  ck_assert_uint_eq(qury_get_int(v), 1);

  ck_assert(qury_get_value(stmt, "name", &v));
  ck_assert_str_eq(qury_get_cstr(v), "alpha");

  ck_assert(qury_get_value(stmt, "flag", &v));
  /* TINY often comes as integer path */
  ck_assert(qury_get_int(v) == 1 || qury_get_bool(v) == true);

  ck_assert(qury_get_value(stmt, "price", &v));
  ck_assert(qury_get_float(v) > 1.4 && qury_get_float(v) < 1.6);

  ck_assert(qury_get_value(stmt, "note", &v));
  ck_assert_str_eq(qury_get_cstr(v), "hello");

  /* only one matching row */
  ck_assert(!qury_fetch(stmt));

  qury_free(stmt);
}
END_TEST

START_TEST(test_null_and_empty_string) {
  require_db();

  qury_stmt_t *stmt = qury_new(&g_conn, NULL);
  ck_assert_ptr_nonnull(stmt);

  ck_assert(qury_prepare(
      stmt, "SELECT id, name, note FROM qury_test_rows WHERE id = :id", 0));

  /* empty string is not NULL */
  ck_assert(qury_stmt_bind_int(stmt, "id", 2));
  ck_assert(qury_execute(stmt));
  ck_assert(qury_fetch(stmt));

  qury_bind_t *v = NULL;
  ck_assert(qury_get_value(stmt, "note", &v));
  ck_assert(!qury_is_null(v));
  ck_assert_str_eq(qury_get_cstr(v), "");

  /* SQL NULL */
  ck_assert(qury_stmt_bind_int(stmt, "id", 3));
  /* re-bind invalidates params_bounded; need re-execute. After previous
   * fetch, re-execute same prepared stmt. */
  ck_assert(qury_execute(stmt));
  ck_assert(qury_fetch(stmt));

  ck_assert(!qury_get_value(stmt, "note", &v));
  qury_bind_t *raw = qury_get_field_value(stmt, "note", NULL);
  ck_assert_ptr_nonnull(raw);
  ck_assert(qury_is_null(raw));

  qury_free(stmt);
}
END_TEST

START_TEST(test_rebind_and_reset) {
  require_db();

  qury_stmt_t *stmt = qury_new(&g_conn, NULL);
  ck_assert_ptr_nonnull(stmt);

  ck_assert(qury_prepare(
      stmt, "SELECT name FROM qury_test_rows WHERE id = :id", 0));

  ck_assert(qury_stmt_bind_int(stmt, "id", 1));
  ck_assert(qury_execute(stmt));
  ck_assert(qury_fetch(stmt));
  qury_bind_t *v = NULL;
  ck_assert(qury_get_value(stmt, "name", &v));
  ck_assert_str_eq(qury_get_cstr(v), "alpha");

  /* same statement, new bind value */
  ck_assert(qury_stmt_bind_int(stmt, "id", 2));
  ck_assert(qury_execute(stmt));
  ck_assert(qury_fetch(stmt));
  ck_assert(qury_get_value(stmt, "name", &v));
  ck_assert_str_eq(qury_get_cstr(v), "beta");

  qury_reset(stmt);

  ck_assert(qury_prepare(
      stmt, "SELECT COUNT(*) AS c FROM qury_test_rows WHERE flag = :f", 0));
  ck_assert(qury_stmt_bind_bool(stmt, "f", true));
  ck_assert(qury_execute(stmt));
  ck_assert(qury_fetch(stmt));
  ck_assert(qury_get_value(stmt, "c", &v));
  ck_assert_uint_eq(qury_get_int(v), 2);

  qury_free(stmt);
}
END_TEST

START_TEST(test_string_bind_and_rebind) {
  require_db();

  qury_stmt_t *stmt = qury_new(&g_conn, NULL);
  ck_assert_ptr_nonnull(stmt);

  ck_assert(qury_prepare(
      stmt, "SELECT id FROM qury_test_rows WHERE name = :name", 0));
  ck_assert(qury_stmt_bind_str(stmt, "name", "beta"));
  ck_assert(qury_execute(stmt));
  ck_assert(qury_fetch(stmt));

  qury_bind_t *v = NULL;
  ck_assert(qury_get_value(stmt, "id", &v));
  ck_assert_uint_eq(qury_get_int(v), 2);

  /* re-bind string must not leak under malloc allocator (ASan/valgrind) */
  ck_assert(qury_stmt_bind_str(stmt, "name", "gamma"));
  ck_assert(qury_execute(stmt));
  ck_assert(qury_fetch(stmt));
  ck_assert(qury_get_value(stmt, "id", &v));
  ck_assert_uint_eq(qury_get_int(v), 3);

  qury_free(stmt);
}
END_TEST

START_TEST(test_select_db_cache) {
  require_db();

  const char *db = env_or("QURY_TEST_DB", "test");
  ck_assert(qury_select_db(&g_conn, db));
  /* second call with same name is a no-op success */
  ck_assert(qury_select_db(&g_conn, db));
  ck_assert(qury_select_db(&g_conn, NULL)); /* clear cache */
  ck_assert(qury_select_db(&g_conn, db));
}
END_TEST

START_TEST(test_bump_allocator_prepare_bind) {
  require_db();

  test_bump_t bump;
  ck_assert(test_bump_init(&bump, 256 * 1024));
  qury_init(&test_bump_allocator);

  qury_stmt_t *stmt = qury_new(&g_conn, &bump);
  ck_assert_ptr_nonnull(stmt);

  ck_assert(qury_prepare(
      stmt, "SELECT name FROM qury_test_rows WHERE id = :id", 0));
  ck_assert(qury_stmt_bind_int(stmt, "id", 1));
  ck_assert(qury_execute(stmt));
  ck_assert(qury_fetch(stmt));

  qury_bind_t *v = NULL;
  ck_assert(qury_get_value(stmt, "name", &v));
  ck_assert_str_eq(qury_get_cstr(v), "alpha");

  /* free is NULL: qury_free must not crash; region owns memory */
  qury_free(stmt);
  test_bump_reset(&bump);
  test_bump_destroy(&bump);

  /* restore default malloc allocator for later cases / other suites */
  qury_init(&test_malloc_allocator);
}
END_TEST

static Suite *qury_suite(void) {
  Suite *s = suite_create("quaerimus");

  TCase *tc = tcase_create("integration");
  tcase_add_unchecked_fixture(tc, setup_conn, teardown_conn);
  tcase_set_timeout(tc, 30);

  if (integration_enabled()) {
    tcase_add_test(tc, test_named_param_rewrite);
    tcase_add_test(tc, test_named_param_edge_forms);
    tcase_add_test(tc, test_bind_execute_fetch_types);
    tcase_add_test(tc, test_null_and_empty_string);
    tcase_add_test(tc, test_rebind_and_reset);
    tcase_add_test(tc, test_string_bind_and_rebind);
    tcase_add_test(tc, test_select_db_cache);
    tcase_add_test(tc, test_bump_allocator_prepare_bind);
  } else {
    fprintf(stderr,
            "test_qury: QURY_TEST_USER not set — integration tests skipped\n");
  }

  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  Suite *s = qury_suite();
  SRunner *sr = srunner_create(s);
  srunner_set_fork_status(sr, CK_NOFORK);
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  if (!integration_enabled()) {
    /* Not a failure: unit-only CI can run without a DB. */
    return EXIT_SUCCESS;
  }
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
