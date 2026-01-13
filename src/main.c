#include "include/quaerimus.h"
#include <mariadb/mysql.h>
#include <openssl/rand.h>

int main(void) {

  mysql_library_init(0, NULL, NULL);
  qury_conn_t conn = {0};
  qury_conn_init(&conn);

  if (!mysql_real_connect(&conn.mysql, "localhost", "phpmyadmin", "phpmyadmin",
                          "bexio_copy", 0, NULL, 0)) {
    fprintf(stderr, "mysql_real_connect : %s\n", mysql_error(&conn.mysql));
  }
  if (mysql_ping(&conn.mysql)) {
    fprintf(stderr, "mysql_ping : %s\n", mysql_error(&conn.mysql));
  }
  qury_stmt_t *stmt = qury_new(&conn);
  qury_prepare(stmt,
               "SELECT *, invoice.id AS AFDS FROM invoice LEFT JOIN pr_project "
               "AS p ON p.id = project_id "
               "WHERE project_id > :prj_id",
               0);
  for (int i = 109; i < 111; i++) {
    qury_stmt_bind_int(stmt, "prj_id", i);
    qury_stmt_dump(stdout, stmt);
    qury_execute(stmt);

    while (qury_fetch(stmt)) {
      qury_bind_t *x = qury_get_field_value(stmt, "nr");
      if (x) {
        printf("VALUE found %s\n", x->value.cstr);
      }
    };
    qury_stmt_reset(stmt);
  }
  qury_stmt_free(stmt);
  mysql_close(&conn.mysql);
  mysql_library_end();
  return 0;
}
