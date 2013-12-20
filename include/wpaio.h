#ifndef _WPAIO_H_
#define _WPAIO_H_

/**
 * Copyright 2013 Da Zheng
 *
 * This file is part of SAFSlib.
 *
 * SAFSlib is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SAFSlib is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SAFSlib.  If not, see <http://www.gnu.org/licenses/>.
 */

# ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/param.h>
#include <fcntl.h>
#include <stdlib.h>
#include <libaio.h>

#include "slab_allocator.h"

#define A_READ 0
#define A_WRITE 1

class aio_ctx
{
	obj_allocator<struct iocb> iocb_allocator;
public:
	aio_ctx(int node_id, int max_aio): iocb_allocator(std::string(
				"iocb_allocator-") + itoa(node_id), node_id,
			sizeof(struct iocb) * max_aio, params.get_max_obj_alloc_size()) {
	}
	virtual ~aio_ctx() {
	}

	struct iocb* make_io_request(int fd, size_t iosize, long long offset,
			void* buffer, int io_type, struct io_callback_s *cb);
	struct iocb *make_iovec_request(int fd, const struct iovec iov[],
			int count, long long offset, int io_type, struct io_callback_s *cb);
	void destroy_io_requests(struct iocb **iocbs, int num) {
		iocb_allocator.free(iocbs, num);
	}

	virtual void submit_io_request(struct iocb* ioq[], int num) = 0;
	virtual int io_wait(struct timespec* to, int num) = 0;
	virtual int max_io_slot() = 0;
	virtual void print_stat() {
	}
};

class aio_ctx_impl: public aio_ctx
{
	int max_aio;
	int busy_aio;
	io_context_t ctx;

public:
	aio_ctx_impl(int node_id, int max_aio): aio_ctx(node_id, max_aio) {
		this->max_aio = max_aio;
		busy_aio = 0;
		memset(&ctx, 0, sizeof(ctx));

		int ret = io_queue_init(max_aio, &ctx);
		if (ret < 0) {
			perror ("io_queue_init");
			exit (1);
		}
	}

	virtual void submit_io_request(struct iocb* ioq[], int num);
	virtual int io_wait(struct timespec* to, int num);
	virtual int max_io_slot();
};

typedef void (*callback_t) (io_context_t, struct iocb*[],
		void *[], long *, long *, int);

struct io_callback_s
{
	callback_t func;
};

#endif
