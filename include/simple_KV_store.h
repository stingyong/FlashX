#ifndef __SIMPLE_KV_STORE_H__
#define __SIMPLE_KV_STORE_H__

/**
 * Copyright 2014 Open Connectome Project (http://openconnecto.me)
 * Written by Da Zheng (zhengda1936@gmail.com)
 *
 * This file is part of SAFSlib.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "io_interface.h"
#include "container.h"
#include "cache.h"
#include "slab_allocator.h"

template<class ValueType, class ValueTaskType>
class KV_compute: public user_compute
{
	embedded_array<ValueTaskType> tasks;
	int num_tasks;
	bool has_run;
public:
	KV_compute(compute_allocator *alloc): user_compute(alloc) {
		num_tasks = 0;
		has_run = false;
	}

	bool has_tasks() const {
		return num_tasks > 0;
	}

	void add_task(const ValueTaskType &task) {
		if (tasks.get_capacity() <= num_tasks)
			tasks.resize(num_tasks * 2);
		tasks[num_tasks++] = task;
		has_run = false;
	}

	virtual int serialize(char *buf, int size) const {
		return 0;
	}

	virtual int get_serialized_size() const {
		return 0;
	}

	virtual void run(page_byte_array &arr) {
		off_t start_off = arr.get_offset() / sizeof(ValueType);
		off_t end_off = (arr.get_offset() + arr.get_size()) / sizeof(ValueType);
		for (int i = 0; i < num_tasks; i++) {
			off_t idx = tasks[i].get_idx();
			assert(idx >= start_off && idx < end_off);
			int num_entries = tasks[i].get_num_entries();
			ValueType vs[num_entries];
			arr.memcpy((idx - start_off) * sizeof(ValueType), (char *) vs,
					sizeof(ValueType) * num_entries);
			tasks[i].run(vs, num_entries);
		}
		has_run = true;
	}

	virtual bool has_completed() const {
		return has_run;
	}

	virtual int has_requests() const {
		return false;
	}

	virtual request_range get_next_request() {
		assert(0);
	}
};

template<class ValueType, class ValueTaskType>
class KV_compute_allocator: public compute_allocator
{
	class compute_initializer: public obj_initiator<KV_compute<ValueType, ValueTaskType> >
	{
		KV_compute_allocator<ValueType, ValueTaskType> *alloc;
	public:
		compute_initializer(
				KV_compute_allocator<ValueType, ValueTaskType> *alloc) {
			this->alloc = alloc;
		}

		virtual void init(KV_compute<ValueType, ValueTaskType> *obj) {
			new (obj) KV_compute<ValueType, ValueTaskType>(alloc);
		}
	};

	class compute_destructor: public obj_destructor<KV_compute<ValueType, ValueTaskType> >
	{
	public:
		void destroy(KV_compute<ValueType, ValueTaskType> *obj) {
			obj->~KV_compute<ValueType, ValueTaskType>();
		}
	};

	obj_allocator<KV_compute<ValueType, ValueTaskType> > allocator;
public:
	KV_compute_allocator(int node_id): allocator("KV_compute_allocator",
			node_id, 1024 * 1024, params.get_max_obj_alloc_size(),
			typename obj_initiator<KV_compute<ValueType, ValueTaskType> >::ptr(
				new compute_initializer(this)),
			typename obj_destructor<KV_compute<ValueType, ValueTaskType> >::ptr(
				new compute_destructor())) {
	}

	virtual user_compute *alloc() {
		return allocator.alloc_obj();
	}

	virtual void free(user_compute *obj) {
		allocator.free((KV_compute<ValueType, ValueTaskType> *) obj);
	}
};

/**
 * This is a simple key-value store over a single file.
 * It supports only one type of user-defined tasks on values and can be used
 * in one thread.
 * User tasks are executed asynchronously.
 */
template<class ValueType, class TaskType>
class simple_KV_store
{
	io_interface::ptr io;
	KV_compute_allocator<ValueType, TaskType> alloc;

	struct task_comp {
		bool operator()(const TaskType &task1, const TaskType &task2) {
			return task1.get_idx() > task2.get_idx();
		}
	};

	std::priority_queue<TaskType, std::vector<TaskType>, task_comp> task_buf;

	embedded_array<io_request> req_buf;
	int num_reqs;

	void add_io_request(io_request &req) {
		if (req_buf.get_capacity() <= num_reqs)
			req_buf.resize(req_buf.get_capacity() * 2);
		req_buf[num_reqs] = req;
		num_reqs++;
	}

	void flush_io_requests() {
		io->access(req_buf.data(), num_reqs);
		num_reqs = 0;
	}

	simple_KV_store(io_interface::ptr io): alloc(io->get_node_id()) {
		this->io = io;
		num_reqs = 0;
		assert(PAGE_SIZE % sizeof(ValueType) == 0);
	}
public:
	typedef std::shared_ptr<simple_KV_store<ValueType, TaskType> > ptr;

	static ptr create(io_interface::ptr io) {
		return ptr(new simple_KV_store<ValueType, TaskType>(io));
	}

	void flush_requests() {
		if (task_buf.empty())
			return;

		// Each time we issue a single request to serve as many user tasks
		// as possible. Each time we issue a request to read at least one
		// page. We'll merge requests if the pages touched by user tasks
		// in the input array are adjacent to each other.

		// The offset of the first page accessed by the I/O request.
		const TaskType &task = task_buf.top();
		task_buf.pop();
		off_t first_page_off = ROUND_PAGE(task.get_idx() * sizeof(ValueType));
		// The offset of the last page accessed by the I/O request.
		// The page is excluded by the I/O request.
		off_t last_page_off = ROUNDUP_PAGE((task.get_idx()
					+ task.get_num_entries()) * sizeof(ValueType));
		KV_compute<ValueType, TaskType> *compute
			= (KV_compute<ValueType, TaskType> *) alloc.alloc();
		compute->add_task(task);
		while (!task_buf.empty()) {
			const TaskType &task = task_buf.top();
			task_buf.pop();
			off_t end_page_off = ROUNDUP_PAGE((task.get_idx()
					+ task.get_num_entries()) * sizeof(ValueType));
			// If the task access the page covered by the I/O request.
			if (end_page_off <= last_page_off) {
				compute->add_task(task);
				continue;
			}
			// If the task access the page right behind the range covered
			// by the I/O request.
			else if (end_page_off == last_page_off + PAGE_SIZE) {
				last_page_off = end_page_off;
				compute->add_task(task);
				continue;
			}

			// The user task accesses a page far away from the range covered
			// by the current I/O request.
			data_loc_t loc(io->get_file_id(), first_page_off);
			io_request req(compute, loc, last_page_off - first_page_off, READ);
			add_io_request(req);

			// Re-initialize the range covered by the new I/O request.
			compute = (KV_compute<ValueType, TaskType> *) alloc.alloc();
			compute->add_task(task);
			first_page_off = ROUND_PAGE(task.get_idx() * sizeof(ValueType));
			last_page_off = end_page_off;
		}

		assert(compute->has_tasks());
		data_loc_t loc(io->get_file_id(), first_page_off);
		io_request req(compute, loc, last_page_off - first_page_off, READ);
		add_io_request(req);
		flush_io_requests();
	}

	/**
	 * Serve user requests asynchronously.
	 */
	void async_request(TaskType &task) {
		task_buf.push(task);
	}
};

#endif
