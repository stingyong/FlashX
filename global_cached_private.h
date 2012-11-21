#ifndef __GLOBAL_CACHED_PRIVATE_H__
#define __GLOBAL_CACHED_PRIVATE_H__

#include "read_private.h"
#include "cache.h"
#include "associative_cache.h"
#include "hash_index_cache.h"
#include "LRU2Q.h"

enum {
	TREE_CACHE,
	ASSOCIATIVE_CACHE,
	HASH_INDEX_CACHE,
	CUCKOO_CACHE,
	LRU2Q_CACHE,
	GCLOCK_CACHE,
};

class global_cached_io: public io_interface
{
	int num_waits;
	long cache_size;
	static page_cache *global_cache;
	/* the underlying IO. */
	io_interface *underlying;
	callback *cb;

	int cache_hits;
public:
	inline global_cached_io(io_interface *underlying) {
		this->underlying = underlying;
		num_waits = 0;
		cache_size = 0;
		cb = NULL;
		cache_hits = 0;
	}

	static page_cache *create_cache(int cache_type, long cache_size) {
		page_cache *global_cache;
		switch (cache_type) {
#if 0
			// These are just for testing in the single thread.
			case TREE_CACHE:
				global_cache = new tree_cache(cache_size, 0);
				break;
			case CUCKOO_CACHE:
				global_cache = new cuckoo_cache(cache_size);
				break;
			case GCLOCK_CACHE:
				global_cache = new gclock_cache(cache_size);
				break;
#endif
			case LRU2Q_CACHE:
				global_cache = new LRU2Q_cache(cache_size);
				break;
			case ASSOCIATIVE_CACHE:
				global_cache = new associative_cache(cache_size);
				break;
			case HASH_INDEX_CACHE:
				global_cache = new hash_index_cache(cache_size);
				break;
			default:
				fprintf(stderr, "wrong cache type\n");
				exit(1);
		}
		return global_cache;
	}

	global_cached_io(io_interface *, long, int);

	virtual page_cache *get_global_cache() {
		return global_cache;
	}

	int preload(off_t start, long size);
	ssize_t access(char *buf, off_t offset, ssize_t size, int access_method);
	ssize_t access(io_request *requests, int num);

	ssize_t get_size() {
		return underlying->get_size();
	}

	/* When a thread begins, this method will be called. */
	virtual int init() {
		global_cache->init();
		return underlying->init();
	}

	virtual bool set_callback(callback *cb) {
		if (underlying->support_aio())
			this->cb = cb;
		return underlying->support_aio();
	}
	
	virtual callback *get_callback() {
		return cb;
	}

	virtual bool support_aio() {
		return underlying->support_aio();
	}

	virtual void cleanup() {
		underlying->cleanup();
	}

#ifdef STATISTICS
	void print_stat() {
		static int tot_hits = 0;
		static int seen_threads = 0;
		seen_threads++;
		tot_hits += cache_hits;
		if (seen_threads == nthreads)
			printf("there are %d cache hits\n", tot_hits);
		printf("there are %d waits\n", num_waits);
	}
#endif
};

#endif
