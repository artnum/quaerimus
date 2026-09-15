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

#define _PIECE_LIT 0
#define _PIECE_PARAM 1
#define _PIECE_STAR 2

#define _LDAP_MAX_PIECES 64

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    void *alloc;
    bool error;
} qbuf_t;

typedef struct {
    int kind;
    size_t off;
    size_t n;
    const char *param;
} filter_piece_t;

typedef struct {
    const char *s;
    size_t n;
    size_t i;
    qbuf_t *out;
    void *alloc;
    bool error;
} ldap_ctx_t;

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

static void _emit_param(qbuf_t *out, const filter_piece_t *p) {
    _qbuf_putc(out, ':');
    _qbuf_put(out, p->param, p->n);
}

static void _emit_like(qbuf_t *out, const filter_piece_t *pieces, size_t np,
                       const char *lits, bool wrap) {
    size_t i;
    bool has_param = false;
    bool star_first = np > 0 && pieces[0].kind == _PIECE_STAR;
    bool star_last = np > 0 && pieces[np - 1].kind == _PIECE_STAR;
    bool first = true;

    for (i = 0; i < np; i++) {
        if (pieces[i].kind == _PIECE_PARAM) {
            has_param = true;
            break;
        }
    }

    if (!has_param) {
        _qbuf_putc(out, '\'');
        if (wrap && !star_first) {
            _qbuf_putc(out, '%');
        }
        for (i = 0; i < np; i++) {
            if (pieces[i].kind == _PIECE_STAR) {
                _qbuf_putc(out, '%');
            } else {
                size_t j;
                for (j = 0; j < pieces[i].n; j++) {
                    char ch = lits[pieces[i].off + j];
                    if (ch == '\'') {
                        _qbuf_putc(out, '\'');
                    }
                    _qbuf_putc(out, ch);
                }
            }
        }
        if (wrap && !star_last) {
            _qbuf_putc(out, '%');
        }
        _qbuf_putc(out, '\'');
        return;
    }

    _qbuf_puts(out, "CONCAT(");
    if (wrap && !star_first) {
        _qbuf_puts(out, "'%'");
        first = false;
    }
    for (i = 0; i < np; i++) {
        if (!first) {
            _qbuf_puts(out, ", ");
        }
        first = false;
        if (pieces[i].kind == _PIECE_STAR) {
            _qbuf_puts(out, "'%'");
        } else if (pieces[i].kind == _PIECE_PARAM) {
            _emit_param(out, &pieces[i]);
        } else {
            _emit_sql_quoted(out, lits + pieces[i].off, pieces[i].n);
        }
    }
    if (wrap && !star_last) {
        if (!first) {
            _qbuf_puts(out, ", ");
        }
        _qbuf_puts(out, "'%'");
    }
    _qbuf_putc(out, ')');
}

static void _emit_scalar(qbuf_t *out, const filter_piece_t *pieces, size_t np,
                         const char *lits) {
    size_t i;
    bool first = true;

    if (np == 0) {
        _qbuf_puts(out, "''");
        return;
    }
    if (np == 1 && pieces[0].kind == _PIECE_PARAM) {
        _emit_param(out, &pieces[0]);
        return;
    }
    if (np == 1 && pieces[0].kind == _PIECE_LIT) {
        if (_is_numeric_lit(lits + pieces[0].off, pieces[0].n)) {
            _qbuf_put(out, lits + pieces[0].off, pieces[0].n);
        } else {
            _emit_sql_quoted(out, lits + pieces[0].off, pieces[0].n);
        }
        return;
    }
    if (np == 1 && pieces[0].kind == _PIECE_STAR) {
        _qbuf_puts(out, "'*'");
        return;
    }

    _qbuf_puts(out, "CONCAT(");
    for (i = 0; i < np; i++) {
        if (!first) {
            _qbuf_puts(out, ", ");
        }
        first = false;
        if (pieces[i].kind == _PIECE_STAR) {
            _qbuf_puts(out, "'*'");
        } else if (pieces[i].kind == _PIECE_PARAM) {
            _emit_param(out, &pieces[i]);
        } else {
            _emit_sql_quoted(out, lits + pieces[i].off, pieces[i].n);
        }
    }
    _qbuf_putc(out, ')');
}

static bool _parse_value_pieces(ldap_ctx_t *c, qbuf_t *litbuf,
                                filter_piece_t *pieces, size_t max_pieces,
                                size_t *npieces) {
    bool in_lit = false;
    size_t lit_start = 0;

    *npieces = 0;

    while (c->i < c->n) {
        unsigned char ch = (unsigned char)c->s[c->i];
        if (ch == ')') {
            break;
        }
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
            if (!in_lit) {
                lit_start = litbuf->len;
                in_lit = true;
            }
            _qbuf_putc(litbuf, (char)((h1 << 4) | h2));
            if (litbuf->error) {
                c->error = true;
                return false;
            }
            c->i += 3;
            continue;
        }
        if (ch == '*') {
            if (in_lit) {
                if (*npieces >= max_pieces) {
                    c->error = true;
                    return false;
                }
                pieces[*npieces].kind = _PIECE_LIT;
                pieces[*npieces].off = lit_start;
                pieces[*npieces].n = litbuf->len - lit_start;
                pieces[*npieces].param = NULL;
                (*npieces)++;
                in_lit = false;
            }
            if (*npieces >= max_pieces) {
                c->error = true;
                return false;
            }
            pieces[*npieces].kind = _PIECE_STAR;
            pieces[*npieces].off = 0;
            pieces[*npieces].n = 0;
            pieces[*npieces].param = NULL;
            (*npieces)++;
            c->i++;
            continue;
        }
        if (ch == ':' && c->i + 1 < c->n
            && _is_variable_char(c->s[c->i + 1])) {
            size_t start;
            if (in_lit) {
                if (*npieces >= max_pieces) {
                    c->error = true;
                    return false;
                }
                pieces[*npieces].kind = _PIECE_LIT;
                pieces[*npieces].off = lit_start;
                pieces[*npieces].n = litbuf->len - lit_start;
                pieces[*npieces].param = NULL;
                (*npieces)++;
                in_lit = false;
            }
            c->i++;
            start = c->i;
            while (c->i < c->n && _is_variable_char(c->s[c->i])) {
                c->i++;
            }
            if (*npieces >= max_pieces) {
                c->error = true;
                return false;
            }
            pieces[*npieces].kind = _PIECE_PARAM;
            pieces[*npieces].off = 0;
            pieces[*npieces].n = c->i - start;
            pieces[*npieces].param = c->s + start;
            (*npieces)++;
            continue;
        }
        if (!in_lit) {
            lit_start = litbuf->len;
            in_lit = true;
        }
        _qbuf_putc(litbuf, (char)ch);
        if (litbuf->error) {
            c->error = true;
            return false;
        }
        c->i++;
    }

    if (in_lit) {
        size_t n = litbuf->len - lit_start;
        /* drop trailing whitespace that sat before the closing ')' */
        while (n > 0 && _is_ws((unsigned char)litbuf->data[lit_start + n - 1])) {
            n--;
        }
        if (n > 0) {
            if (*npieces >= max_pieces) {
                c->error = true;
                return false;
            }
            pieces[*npieces].kind = _PIECE_LIT;
            pieces[*npieces].off = lit_start;
            pieces[*npieces].n = n;
            pieces[*npieces].param = NULL;
            (*npieces)++;
        }
    }
    return true;
}

static bool _parse_ldap_item(ldap_ctx_t *c) {
    size_t attr_s;
    size_t attr_n;
    int op;
    int ch;
    int ch2;
    filter_piece_t pieces[_LDAP_MAX_PIECES];
    size_t np = 0;
    size_t i;
    bool has_star = false;
    qbuf_t litbuf;

    memset(&litbuf, 0, sizeof(litbuf));
    litbuf.alloc = c->alloc;

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

    _ldap_skip_ws(c);
    if (!_parse_value_pieces(c, &litbuf, pieces, _LDAP_MAX_PIECES, &np)) {
        _qbuf_free(&litbuf);
        return false;
    }

    if (op == _LDAP_OP_EQ && np == 1 && pieces[0].kind == _PIECE_STAR) {
        _qbuf_puts(c->out, "NULLIF(");
        _qbuf_put(c->out, c->s + attr_s, attr_n);
        _qbuf_puts(c->out, ", '') IS NOT NULL");
        _qbuf_free(&litbuf);
        if (c->out->error) {
            c->error = true;
            return false;
        }
        return true;
    }

    for (i = 0; i < np; i++) {
        if (pieces[i].kind == _PIECE_STAR) {
            has_star = true;
            break;
        }
    }

    _qbuf_put(c->out, c->s + attr_s, attr_n);

    if (op == _LDAP_OP_APPROX || (op == _LDAP_OP_EQ && has_star)) {
        _qbuf_puts(c->out, " LIKE ");
        _emit_like(c->out, pieces, np, litbuf.data ? litbuf.data : "",
                   op == _LDAP_OP_APPROX);
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
                _qbuf_free(&litbuf);
                return false;
        }
        _emit_scalar(c->out, pieces, np, litbuf.data ? litbuf.data : "");
    }

    _qbuf_free(&litbuf);
    if (c->out->error) {
        c->error = true;
        return false;
    }
    return true;
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

