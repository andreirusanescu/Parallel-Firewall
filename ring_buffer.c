// SPDX-License-Identifier: BSD-3-Clause

#include <utils.h>
#include "ring_buffer.h"
#include "packet.h"

int ring_buffer_init(so_ring_buffer_t *ring, size_t cap)
{
	int ret;

	ring->data = (char *)malloc(cap * sizeof(char));
	DIE(!ring->data, "malloc() failed");
	ret = pthread_mutex_init(&ring->mutex_buffer, NULL);
	DIE(ret != 0, "mutex_init() failed");

	ret = pthread_cond_init(&ring->full_cond, NULL);
	DIE(ret != 0, "cond failed");
	ret = pthread_cond_init(&ring->empty_cond, NULL);
	DIE(ret != 0, "cond failed");

	ring->len = 0;
	ring->cap = cap;
	ring->read_pos = ring->write_pos = 0;
	ring->num_packets = 0;
	return 1;
}

ssize_t ring_buffer_enqueue(so_ring_buffer_t *ring, void *data, size_t size)
{
	if (ring->cap - ring->len < size)
		return -1;

	for (size_t i = 0; i < size; ++i) {
		ring->data[ring->write_pos] = ((char *)data)[i];
		// ring->write_pos = (ring->write_pos + 1) % ring->cap;
		++ring->write_pos;
	}
	ring->write_pos %= ring->cap;

	ring->len += size;
	return size;
}

ssize_t ring_buffer_dequeue(so_ring_buffer_t *ring, void *data, size_t size)
{
	if (size > ring->len)
		return -1;

	for (size_t i = 0; i < size; ++i) {
		((char *)data)[i] = ring->data[ring->read_pos];
		// ring->read_pos = (ring->read_pos + 1) % ring->cap;
		++ring->read_pos;
	}

	ring->read_pos %= ring->cap;
	ring->len -= size;

	return size;
}

void ring_buffer_destroy(so_ring_buffer_t *ring)
{
	int ret;

	free(ring->data);
	ret = pthread_mutex_destroy(&ring->mutex_buffer);
	DIE(ret != 0, "mutex failed");

	ret = pthread_cond_destroy(&ring->full_cond);
	DIE(ret != 0, "cond failed");
	ret = pthread_cond_destroy(&ring->empty_cond);
	DIE(ret != 0, "cond failed");

	ring->len = ring->cap = 0;
	ring->read_pos = ring->write_pos = 0;
}

void ring_buffer_stop(so_ring_buffer_t *ring)
{
	pthread_mutex_lock(&ring->mutex_buffer);
	ring->done = 1;
	pthread_cond_broadcast(&ring->full_cond);
	pthread_mutex_unlock(&ring->mutex_buffer);
}
