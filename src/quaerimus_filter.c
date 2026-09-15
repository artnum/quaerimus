/* LDAP filter translation; public entry is qury_filter(). */

#include "include/quaerimus.h"
#include "quaerimus_internal.h"
#include <assert.h>
#include <string.h>

#define _is_variable_char(c)                                                   \
    (((c) >= '0' && (c) <= '9') || ((c) >= 'a' && (c) <= 'z') ||               \
     ((c) >= 'A' && (c) <= 'Z') || ((c) == '_'))

#define _LDAP_OP_EQ 0
#define _LDAP_OP_APPROX 1
#define _LDAP_OP_GT 2
#define _LDAP_OP_LT 3
#define _LDAP_OP_GE 4
#define _LDAP_OP_LE 5

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    void *alloc;
    bool error;
} qbuf_t;

typedef struct {
    const char *s;
    size_t n;
    size_t i;
    qbuf_t *out;
    void *alloc;
    bool error;
} ldap_ctx_t;

/* Assertion value: either a whole :param, or a decoded literal.
 * Unescaped LDAP * is stored as '%' in lit (SQL LIKE wildcard). */
typedef struct {
    bool is_param;
    bool has_star;
    const char *param;
    size_t param_n;
    qbuf_t lit;
} ldap_val_t;

static bool _parse_ldap_filter(ldap_ctx_t *c);

static bool _is_ws(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool _qbuf_reserve(qbuf_t *b, size_t extra) {
    if (!b || b->error) {
        return false;
    }
    size_t need = b->len + extra + 1;
    if (need <= b->cap) {
        return true;
    }
    size_t ncap = b->cap ? b->cap : 128;
    while (ncap < need) {
        if (ncap > ((size_t)-1) / 2) {
            b->error = true;
            return false;
        }
        ncap *= 2;
    }
    char *nd = MemoryAllocator->realloc(b->alloc, b->data, ncap);
    if (!nd) {
        b->error = true;
        return false;
    }
    b->data = nd;
    b->cap = ncap;
    if (b->len == 0) {
        b->data[0] = '\0';
    }
    return true;
}

static void _qbuf_put(qbuf_t *b, const char *s, size_t n) {
    if (!b || b->error || n == 0) {
        return;
    }
    if (!_qbuf_reserve(b, n)) {
        return;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void _qbuf_puts(qbuf_t *b, const char *s) {
    if (!s) {
        return;
    }
    _qbuf_put(b, s, strlen(s));
}

static void _qbuf_putc(qbuf_t *b, char c) {
    _qbuf_put(b, &c, 1);
}

static void _qbuf_free(qbuf_t *b) {
    if (!b) {
        return;
    }
    if (b->data && MemoryAllocator->free) {
        MemoryAllocator->free(b->alloc, b->data);
    }
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void _ldap_skip_ws(ldap_ctx_t *c) {
    while (c->i < c->n && _is_ws((unsigned char)c->s[c->i])) {
        c->i++;
    }
}

static int _ldap_peek(const ldap_ctx_t *c) {
    if (c->i >= c->n) {
        return -1;
    }
    return (unsigned char)c->s[c->i];
}

static int _ldap_hex(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool _is_ldap_attr_char(int c) {
    if (c <= 0) {
        return false;
    }
    switch (c) {
        case '=':
        case '~':
        case '<':
        case '>':
        case '(':
        case ')':
        case '&':
        case '|':
        case '!':
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            return false;
        default:
            return true;
    }
}

static bool _is_numeric_lit(const char *p, size_t n) {
    size_t i = 0;
    bool dot = false;
    bool digit = false;

    if (n == 0) {
        return false;
    }
    if (p[0] == '-' || p[0] == '+') {
        i++;
    }
    if (i >= n) {
        return false;
    }
    for (; i < n; i++) {
        if (p[i] >= '0' && p[i] <= '9') {
            digit = true;
        } else if (p[i] == '.' && !dot) {
            dot = true;
        } else {
            return false;
        }
    }
    return digit;
}

static void _emit_sql_quoted(qbuf_t *out, const char *p, size_t n) {
    size_t i;
    _qbuf_putc(out, '\'');
    for (i = 0; i < n; i++) {
        if (p[i] == '\'') {
            _qbuf_putc(out, '\'');
        }
        _qbuf_putc(out, p[i]);
    }
    _qbuf_putc(out, '\'');
}

static void _emit_param(qbuf_t *out, const ldap_val_t *v) {
    _qbuf_putc(out, ':');
    _qbuf_put(out, v->param, v->param_n);
}

static void _emit_like_lit(qbuf_t *out, const ldap_val_t *v, bool wrap) {
    const char *p = v->lit.data;
    size_t n = v->lit.len;
    bool pct_first = n > 0 && p[0] == '%';
    bool pct_last = n > 0 && p[n - 1] == '%';
    size_t i;

    _qbuf_putc(out, '\'');
    if (wrap && !pct_first) {
        _qbuf_putc(out, '%');
    }
    for (i = 0; i < n; i++) {
        if (p[i] == '\'') {
            _qbuf_putc(out, '\'');
        }
        _qbuf_putc(out, p[i]);
    }
    if (wrap && !pct_last) {
        _qbuf_putc(out, '%');
    }
    _qbuf_putc(out, '\'');
}

static void _emit_scalar(qbuf_t *out, const ldap_val_t *v) {
    if (v->is_param) {
        _emit_param(out, v);
        return;
    }
    if (_is_numeric_lit(v->lit.data, v->lit.len)) {
        _qbuf_put(out, v->lit.data, v->lit.len);
        return;
    }
    _emit_sql_quoted(out, v->lit.data, v->lit.len);
}

static bool _parse_ldap_value(ldap_ctx_t *c, ldap_val_t *v) {
    memset(v, 0, sizeof(*v));
    v->lit.alloc = c->alloc;
    _ldap_skip_ws(c);

    /* Whole value is :name (nothing else before ')'). */
    if (_ldap_peek(c) == ':' && c->i + 1 < c->n
        && _is_variable_char(c->s[c->i + 1])) {
        size_t save = c->i;
        size_t start;
        size_t n;

        c->i++;
        start = c->i;
        while (c->i < c->n && _is_variable_char(c->s[c->i])) {
            c->i++;
        }
        n = c->i - start;
        _ldap_skip_ws(c);
        if (n > 0 && _ldap_peek(c) == ')') {
            v->is_param = true;
            v->param = c->s + start;
            v->param_n = n;
            return true;
        }
        c->i = save;
    }

    while (c->i < c->n && c->s[c->i] != ')') {
        unsigned char ch = (unsigned char)c->s[c->i];
        if (ch == '\\') {
            int h1;
            int h2;
            if (c->i + 2 >= c->n) {
                c->error = true;
                return false;
            }
            h1 = _ldap_hex((unsigned char)c->s[c->i + 1]);
            h2 = _ldap_hex((unsigned char)c->s[c->i + 2]);
            if (h1 < 0 || h2 < 0) {
                c->error = true;
                return false;
            }
            _qbuf_putc(&v->lit, (char)((h1 << 4) | h2));
            c->i += 3;
            continue;
        }
        if (ch == '*') {
            v->has_star = true;
            _qbuf_putc(&v->lit, '%');
            c->i++;
            continue;
        }
        _qbuf_putc(&v->lit, (char)ch);
        c->i++;
    }

    while (v->lit.len > 0
           && _is_ws((unsigned char)v->lit.data[v->lit.len - 1])) {
        v->lit.len--;
        v->lit.data[v->lit.len] = '\0';
    }
    if (v->lit.error) {
        c->error = true;
        return false;
    }
    return true;
}

static bool _parse_ldap_item(ldap_ctx_t *c) {
    size_t attr_s;
    size_t attr_n;
    int op;
    int ch;
    int ch2;
    ldap_val_t val;
    bool ok;

    _ldap_skip_ws(c);
    attr_s = c->i;
    while (c->i < c->n && _is_ldap_attr_char((unsigned char)c->s[c->i])) {
        c->i++;
    }
    attr_n = c->i - attr_s;
    if (attr_n == 0) {
        c->error = true;
        return false;
    }

    _ldap_skip_ws(c);
    ch = _ldap_peek(c);
    ch2 = (c->i + 1 < c->n) ? (unsigned char)c->s[c->i + 1] : -1;
    if (ch == '~' && ch2 == '=') {
        op = _LDAP_OP_APPROX;
        c->i += 2;
    } else if (ch == '>' && ch2 == '=') {
        op = _LDAP_OP_GE;
        c->i += 2;
    } else if (ch == '<' && ch2 == '=') {
        op = _LDAP_OP_LE;
        c->i += 2;
    } else if (ch == '>') {
        op = _LDAP_OP_GT;
        c->i += 1;
    } else if (ch == '<') {
        op = _LDAP_OP_LT;
        c->i += 1;
    } else if (ch == '=') {
        op = _LDAP_OP_EQ;
        c->i += 1;
    } else {
        c->error = true;
        return false;
    }

    if (!_parse_ldap_value(c, &val)) {
        _qbuf_free(&val.lit);
        return false;
    }

    if (op == _LDAP_OP_EQ && !val.is_param && val.has_star && val.lit.len == 1
        && val.lit.data[0] == '%') {
        _qbuf_puts(c->out, "NULLIF(");
        _qbuf_put(c->out, c->s + attr_s, attr_n);
        _qbuf_puts(c->out, ", '') IS NOT NULL");
        ok = !c->out->error;
        _qbuf_free(&val.lit);
        if (!ok) {
            c->error = true;
        }
        return ok;
    }

    _qbuf_put(c->out, c->s + attr_s, attr_n);

    if (op == _LDAP_OP_APPROX || (op == _LDAP_OP_EQ && val.has_star)) {
        _qbuf_puts(c->out, " LIKE ");
        if (val.is_param) {
            _qbuf_puts(c->out, "CONCAT('%', ");
            _emit_param(c->out, &val);
            _qbuf_puts(c->out, ", '%')");
        } else {
            _emit_like_lit(c->out, &val, op == _LDAP_OP_APPROX);
        }
    } else {
        switch (op) {
            case _LDAP_OP_EQ:
                _qbuf_puts(c->out, " = ");
                break;
            case _LDAP_OP_GT:
                _qbuf_puts(c->out, " > ");
                break;
            case _LDAP_OP_LT:
                _qbuf_puts(c->out, " < ");
                break;
            case _LDAP_OP_GE:
                _qbuf_puts(c->out, " >= ");
                break;
            case _LDAP_OP_LE:
                _qbuf_puts(c->out, " <= ");
                break;
            default:
                c->error = true;
                _qbuf_free(&val.lit);
                return false;
        }
        _emit_scalar(c->out, &val);
    }

    ok = !c->out->error;
    _qbuf_free(&val.lit);
    if (!ok) {
        c->error = true;
    }
    return ok;
}

static bool _parse_ldap_filter_list(ldap_ctx_t *c, const char *op_sql) {
    bool first = true;

    _qbuf_putc(c->out, '(');
    for (;;) {
        _ldap_skip_ws(c);
        if (_ldap_peek(c) != '(') {
            break;
        }
        if (!first) {
            _qbuf_putc(c->out, ' ');
            _qbuf_puts(c->out, op_sql);
            _qbuf_putc(c->out, ' ');
        }
        first = false;
        if (!_parse_ldap_filter(c)) {
            return false;
        }
    }
    if (first) {
        c->error = true;
        return false;
    }
    _qbuf_putc(c->out, ')');
    if (c->out->error) {
        c->error = true;
        return false;
    }
    return true;
}

static bool _parse_ldap_filter(ldap_ctx_t *c) {
    int ch;

    _ldap_skip_ws(c);
    if (_ldap_peek(c) != '(') {
        c->error = true;
        return false;
    }
    c->i++;
    _ldap_skip_ws(c);
    ch = _ldap_peek(c);
    if (ch == '&') {
        c->i++;
        if (!_parse_ldap_filter_list(c, "AND")) {
            return false;
        }
    } else if (ch == '|') {
        c->i++;
        if (!_parse_ldap_filter_list(c, "OR")) {
            return false;
        }
    } else if (ch == '!') {
        c->i++;
        _qbuf_puts(c->out, "NOT (");
        if (!_parse_ldap_filter(c)) {
            return false;
        }
        _qbuf_putc(c->out, ')');
    } else {
        if (!_parse_ldap_item(c)) {
            return false;
        }
    }
    _ldap_skip_ws(c);
    if (_ldap_peek(c) != ')') {
        c->error = true;
        return false;
    }
    c->i++;
    if (c->out->error) {
        c->error = true;
        return false;
    }
    return true;
}

bool qury_filter(qury_stmt_t *stmt, const char *table, size_t table_len,
                 const char *filter, size_t filter_len) {
    qbuf_t where;
    qbuf_t sql;
    ldap_ctx_t ctx;
    bool has_where = false;
    const char *cols;
    size_t cols_len;
    bool ok;

    assert(stmt != NULL);
    assert(table != NULL);

    if (table_len == 0) {
        table_len = strlen(table);
    }
    if (table_len == 0) {
        return false;
    }
    if (!filter) {
        filter = "";
        filter_len = 0;
    } else if (filter_len == 0) {
        filter_len = strlen(filter);
    }

    memset(&where, 0, sizeof(where));
    memset(&sql, 0, sizeof(sql));
    where.alloc = stmt->allocator;
    sql.alloc = stmt->allocator;

    memset(&ctx, 0, sizeof(ctx));
    ctx.s = filter;
    ctx.n = filter_len;
    ctx.alloc = stmt->allocator;
    ctx.out = &where;

    _ldap_skip_ws(&ctx);
    if (_ldap_peek(&ctx) == '(') {
        if (!_parse_ldap_filter(&ctx) || ctx.error || where.error) {
            _qbuf_free(&where);
            return false;
        }
        has_where = where.len > 0;
    }

    _ldap_skip_ws(&ctx);
    cols = filter + ctx.i;
    cols_len = (ctx.i <= filter_len) ? (filter_len - ctx.i) : 0;
    while (cols_len > 0 && _is_ws((unsigned char)cols[cols_len - 1])) {
        cols_len--;
    }

    _qbuf_puts(&sql, "SELECT ");
    if (cols_len == 0) {
        _qbuf_putc(&sql, '*');
    } else {
        _qbuf_put(&sql, cols, cols_len);
    }
    _qbuf_puts(&sql, " FROM ");
    _qbuf_put(&sql, table, table_len);
    if (has_where) {
        _qbuf_puts(&sql, " WHERE ");
        _qbuf_put(&sql, where.data, where.len);
    }

    ok = !sql.error && sql.data != NULL
         && qury_prepare(stmt, sql.data, sql.len);

    _qbuf_free(&sql);
    _qbuf_free(&where);
    return ok;
}

