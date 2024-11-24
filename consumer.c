// SPDX-License-Identifier: BSD-3-Clause

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

#include "consumer.h"
#include "ring_buffer.h"
#include "packet.h"
#include "utils.h"

static pthread_mutex_t mutex_write;
static pthread_cond_t cond_write;
static pthread_mutex_t mutex_wait;
static pthread_cond_t cond_wait;
static int threads_writing;
static int threads_waiting;
static int done;

int compare_packets(const void *a, const void *b)
{
	packet_data_t *pa = *(packet_data_t **)a;
	packet_data_t *pb = *(packet_data_t **)b;

	if (pa->timestamp < pb->timestamp)
		return -1;
	else if (pa->timestamp > pb->timestamp)
		return 1;
	return 0;
}

void *consumer_thread(void *arg)
{
	so_consumer_ctx_t *ctx = (so_consumer_ctx_t *)arg;
	char packet[PKT_SZ], out_buf[PKT_SZ];
	so_packet_t *pkt;
	int ret, len;

	while (true) {
		pthread_mutex_lock(&ctx->producer_rb->mutex_buffer);
		while (ctx->producer_rb->len == 0 && !ctx->producer_rb->done)
			pthread_cond_wait(&ctx->producer_rb->full_cond,
							  &ctx->producer_rb->mutex_buffer);

		if (ctx->producer_rb->done && ctx->producer_rb->len == 0) {
			pthread_mutex_unlock(&ctx->producer_rb->mutex_buffer);
			ctx->buffer[ctx->thread_id]->timestamp = __INT_MAX__;
			goto barrier;
		}

		ret = ring_buffer_dequeue(ctx->producer_rb, packet, PKT_SZ);
		if (ret == -1) {
			pthread_mutex_unlock(&ctx->producer_rb->mutex_buffer);
			ctx->buffer[ctx->thread_id]->timestamp = __INT_MAX__;
			goto barrier;
		}

		pkt = (struct so_packet_t *)packet;

		pthread_cond_signal(&ctx->producer_rb->empty_cond);
		pthread_mutex_unlock(&ctx->producer_rb->mutex_buffer);

		/* process item */
		ctx->buffer[ctx->thread_id]->action = process_packet(pkt);
		ctx->buffer[ctx->thread_id]->hash = packet_hash(pkt);
		ctx->buffer[ctx->thread_id]->timestamp = pkt->hdr.timestamp;

barrier:
		pthread_mutex_lock(&mutex_write);
		threads_writing++;
		if (threads_writing == ctx->num_threads) {
			qsort(ctx->buffer, ctx->num_threads, sizeof(packet_data_t *), compare_packets);

			int n = ctx->producer_rb->num_packets > ctx->num_threads
						? ctx->num_threads : ctx->producer_rb->num_packets;

			if (n < ctx->num_threads)
				done = 1;
			ctx->producer_rb->num_packets -= n;

			for (int i = 0; i < n; ++i) {
				len = snprintf(out_buf, PKT_SZ, "%s %016lx %lu\n",
							   RES_TO_STR(ctx->buffer[i]->action),
							   ctx->buffer[i]->hash, ctx->buffer[i]->timestamp);
				write(ctx->out_fd, out_buf, len);
			}

			threads_writing = 0;
			pthread_cond_broadcast(&cond_write);
		} else {
			pthread_cond_wait(&cond_write, &mutex_write);
		}
		pthread_mutex_unlock(&mutex_write);

		pthread_mutex_lock(&mutex_wait);
		threads_waiting++;
		if (threads_waiting == ctx->num_threads) {
			threads_waiting = 0;
			pthread_cond_broadcast(&cond_wait);
		} else {
			pthread_cond_wait(&cond_wait, &mutex_wait);
		}

		pthread_mutex_unlock(&mutex_wait);

		if (done)
			break;
	}

	pthread_exit(NULL);
}

so_consumer_ctx_t **create_consumers(pthread_t *tids,
					 int num_consumers,
					 struct so_ring_buffer_t *rb,
					 const char *out_filename)
{
	int ret;
	int out_fd = open(out_filename, O_RDWR|O_CREAT|O_TRUNC, 0666);
	packet_data_t **buffer = (packet_data_t **)malloc(num_consumers * sizeof(packet_data_t *));

	for (int i = 0; i < num_consumers; ++i)
		buffer[i] = malloc(sizeof(packet_data_t));

	ret = pthread_mutex_init(&mutex_write, NULL);
	DIE(ret != 0, "mutex failed");
	ret = pthread_mutex_init(&mutex_wait, NULL);
	DIE(ret != 0, "mutex failed");
	ret = pthread_cond_init(&cond_write, NULL);
	DIE(ret != 0, "cond failed");
	ret = pthread_cond_init(&cond_wait, NULL);
	DIE(ret != 0, "cond failed");

	so_consumer_ctx_t **ctx = (so_consumer_ctx_t **)malloc((num_consumers) * sizeof(so_consumer_ctx_t *));
	int i;

	for (i = 0; i < num_consumers; ++i) {
		ctx[i] = (so_consumer_ctx_t *)malloc(sizeof(so_consumer_ctx_t));
		ctx[i]->producer_rb = rb;
		ctx[i]->num_threads = num_consumers;
		ctx[i]->out_fd = out_fd;
		ctx[i]->thread_id = i;
		ctx[i]->buffer = buffer;
		ret = pthread_create(&tids[i], NULL, &consumer_thread, ctx[i]);
		DIE(ret < 0, "pthread failed");
	}

	return ctx;
}
