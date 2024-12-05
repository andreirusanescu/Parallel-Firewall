// SPDX-License-Identifier: BSD-3-Clause

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "ring_buffer.h"
#include "packet.h"
#include "utils.h"
#include "producer.h"

void publish_data(so_ring_buffer_t *rb, const char *filename)
{
	char buffer[PKT_SZ];
	ssize_t sz;
	int fd, ret;
	struct stat info;

	fd = open(filename, O_RDONLY);
	DIE(fd < 0, "open");

	ret = fstat(fd, &info);
	DIE(ret != 0, "fstat failed");

	rb->num_packets = (info.st_size >> 8);

	while ((sz = read(fd, buffer, PKT_SZ)) != 0) {
		DIE(sz != PKT_SZ, "packet truncated");

		pthread_mutex_lock(&rb->mutex_buffer);

		while (rb->len == rb->cap)
			pthread_cond_wait(&rb->empty_cond, &rb->mutex_buffer);

		/* enqueue packet into ring buffer */
		ring_buffer_enqueue(rb, buffer, sz);

		pthread_cond_signal(&rb->full_cond);
		pthread_mutex_unlock(&rb->mutex_buffer);
	}

	close(fd);
	ring_buffer_stop(rb);
}
