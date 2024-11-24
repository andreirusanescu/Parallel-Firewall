/* SPDX-License-Identifier: BSD-3-Clause */
#define _XOPEN_SOURCE 600
#ifndef __SO_CONSUMER_H__
#define __SO_CONSUMER_H__

#include "ring_buffer.h"
#include "packet.h"

typedef struct packet_data_t {
	unsigned long hash, timestamp;
	int action;
} packet_data_t;

typedef struct so_consumer_ctx_t {
	struct so_ring_buffer_t *producer_rb;
	long out_fd;
	long count;

	/* TODO: add synchronization primitives for timestamp ordering */
	int thread_id;
	int num_threads;
	int cnt_buffer;
	packet_data_t **buffer;
} so_consumer_ctx_t;

so_consumer_ctx_t **create_consumers(pthread_t *tids,
					int num_consumers,
					struct so_ring_buffer_t *rb,
					const char *out_filename);

#endif /* __SO_CONSUMER_H__ */
