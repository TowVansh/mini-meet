#include "proto.h"

#include <string.h>

int mm_tokenize(char *line, mm_msg_t *msg) {
    char *p = line;
    msg->argc = 0;
    while (*p && msg->argc < MM_MAX_TOKENS) {
        while (*p == ' ' || *p == '\r' || *p == '\t') p++;
        if (!*p) break;
        msg->argv[msg->argc++] = p;
        while (*p && *p != ' ' && *p != '\r' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return msg->argc;
}

int mm_valid_name(const char *s) {
    size_t n = 0;
    for (; *s; s++, n++) {
        char c = *s;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return n >= 1 && n <= MM_NAME_MAX;
}

int mm_valid_cand_type(const char *s) {
    return strcmp(s, "host") == 0 || strcmp(s, "srflx") == 0;
}

int mm_linebuf_push(mm_linebuf_t *lb, const char *data, size_t n) {
    if (lb->len + n > sizeof lb->buf) return -1;
    memcpy(lb->buf + lb->len, data, n);
    lb->len += n;
    /* A buffer holding MM_LINE_MAX bytes with no newline can never become valid. */
    if (lb->len >= MM_LINE_MAX && !memchr(lb->buf, '\n', lb->len)) return -1;
    return 0;
}

int mm_linebuf_pop(mm_linebuf_t *lb, char *out, size_t cap) {
    char *nl = memchr(lb->buf, '\n', lb->len);
    size_t line_len;
    if (!nl) return 0;
    line_len = (size_t)(nl - lb->buf);
    if (line_len >= cap) line_len = cap - 1;
    memcpy(out, lb->buf, line_len);
    out[line_len] = '\0';
    line_len = (size_t)(nl - lb->buf) + 1;
    memmove(lb->buf, lb->buf + line_len, lb->len - line_len);
    lb->len -= line_len;
    return 1;
}
