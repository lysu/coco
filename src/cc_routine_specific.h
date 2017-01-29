#ifndef LIBCOCO_CC_ROUTINE_SPECIFIC_H
#define LIBCOCO_CC_ROUTINE_SPECIFIC_H

extern int co_setspecific(pthread_key_t key, const void *value);
extern void *co_getspecific(pthread_key_t key);

#define CO_ROUTINE_SPECIFIC(name) \
\
static pthread_once_t _routine_once_##name = PTHREAD_ONCE_INIT; \
static pthread_key_t _routine_key_##name;\
static int _routine_init_##name = 0;\
static void _routine_make_key_##name() {\
 	(void) pthread_key_create(&_routine_key_##name, NULL); \
}\
struct name *routine_data_routine_##name() { \
	if (!_routine_init_##name) { \
		pthread_once(&_routine_once_##name, _routine_make_key_##name); \
		_routine_init_##name = 1; \
	} \
	struct name *p = co_getspecific(_routine_key_##name); \
	if (!p) { \
		p = calloc(1, sizeof(struct name)); \
		int ret = co_setspecific(_routine_key_##name, p); \
		if (ret) { \
			if (p) { \
				free(p); \
				p = NULL; \
			} \
		} \
	} \
	return p; \
}

#endif //LIBCOCO_CC_ROUTINE_SPECIFIC_H
