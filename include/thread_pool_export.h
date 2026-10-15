#ifndef SLEEP_THREAD_POOL_EXPORT_H
#define SLEEP_THREAD_POOL_EXPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExternValueType {
  uint64_t holder;
} ExternValueType;

typedef struct ExternRefType {
  void *ptr;
} ExternRefType;

typedef void (*RefWorkerFunctionPointer)(ExternRefType);

/* Returns NULL if creation fails. Callbacks run on native worker threads. */
void *create_thread_pool_ref(RefWorkerFunctionPointer function_ptr);

/* The caller owns ptr and must keep it valid until the callback finishes. */
void push_value_thread_pool_ref(ExternRefType ref_type, void *thread_pool);

/* Stops and joins workers; queued work is not guaranteed to finish.
   Call from the owning thread, never from a pool callback. NULL is allowed. */
void clean_thread_pool(void *thread_pool);

#ifdef __cplusplus
}
#endif

#endif
