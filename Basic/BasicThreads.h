#pragma once
#include "Basic.h"

struct ThreadPool;

ThreadPool* CreateThreadPool(StackAllocator* alloc);
void ReleaseThreadPool(ThreadPool* thread_pool);

using ThreadPoolCallback = void (*) (void* user_data, u64 index, u32 thread_index);

void ParallelForWithCallback(ThreadPool* thread_pool, u64 begin_index, u64 end_index, u64 batch_size, void* user_data, ThreadPoolCallback callback);

template<typename Lambda>
void ParallelFor(ThreadPool* thread_pool, u64 begin_index, u64 end_index, u64 batch_size, Lambda&& lambda) {
	ParallelForWithCallback(thread_pool, begin_index, end_index, batch_size, &lambda, [](void* user_data, u64 index, u32 thread_index) {
		auto& lambda = *(Lambda*)user_data;
		lambda(index, thread_index);
	});
}
