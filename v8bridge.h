#include <stddef.h>
#include <stdint.h>

#ifndef V8_C_BRIDGER_H
#define V8_C_BRIDGER_H

#define V8_COMPRESS_POINTERS

#ifdef __cplusplus
extern "C" {
#endif

struct worker_s;
typedef struct worker_s worker;

struct buf_s {
  void* data;
  size_t len;
};
typedef struct buf_s bufs;

const char* worker_version();
void worker_set_flags(int* argc, char** argv);

void v8_init();

worker* worker_new(int table_index);

// returns nonzero on error
// get error from worker_last_exception
int worker_load(worker* w, char* name_s, char* source_s);
int worker_load_module(worker* w, char* name_s, char* source_s, int callback_index);

const char* worker_last_exception(worker* w);

int worker_send_bytes(worker* w, void* data, size_t len);

void worker_dispose(worker* w);
void worker_terminate_execution(worker* w);

#ifdef __cplusplus
}
#endif

#endif  // !defined(V8_C_BRIDGE_H)