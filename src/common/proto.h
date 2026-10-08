/* Mini-Meet signalling protocol (text lines over TCP). Spec: docs/PROTOCOL.md
 *
 * Every message is one line of ASCII ending in '\n', at most MM_LINE_MAX
 * bytes, made of space-separated tokens. The first token is the command.
 *
 *   client -> server             server -> client
 *   JOIN <room> <name>           JOINED <self-id> <room> [<id>:<name> ...]
 *   CAND <to> <type> <ip> <port> PEER_JOINED <id> <name>
 *   LEAVE                        PEER_LEFT <id>
 *   PING                         CAND <from> <type> <ip> <port>
 *                                PONG
 *                                ERROR <code> <text...>
 */
#ifndef MM_PROTO_H
#define MM_PROTO_H

#include <stddef.h>

#define MM_PROTO_VERSION 1
#define MM_LINE_MAX      512
#define MM_MAX_TOKENS    16
#define MM_MAX_ROOM      4      /* participants per room */
#define MM_NAME_MAX      32
#define MM_ID_LEN        8      /* hex chars */
#define MM_DEFAULT_PORT  9000

typedef struct {
    int   argc;
    char *argv[MM_MAX_TOKENS];
} mm_msg_t;

/* Splits a line (modified in place) into tokens. Returns token count, 0 for empty. */
int  mm_tokenize(char *line, mm_msg_t *msg);

/* Room and peer names: 1..32 chars of [A-Za-z0-9_-]. */
int  mm_valid_name(const char *s);
/* Candidate type token: "host" or "srflx". */
int  mm_valid_cand_type(const char *s);

/* Line assembler for a TCP byte stream: TCP has no message boundaries,
 * so bytes are buffered until a '\n' completes a line. */
typedef struct {
    char   buf[MM_LINE_MAX * 4];
    size_t len;
} mm_linebuf_t;

/* Append received bytes. Returns -1 if a line exceeds MM_LINE_MAX (protocol error). */
int  mm_linebuf_push(mm_linebuf_t *lb, const char *data, size_t n);
/* Pop next complete line into out (without '\n'). Returns 1 if a line was popped. */
int  mm_linebuf_pop(mm_linebuf_t *lb, char *out, size_t cap);

#endif
