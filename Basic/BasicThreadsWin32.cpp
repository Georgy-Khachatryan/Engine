#include "BasicThreads.h"
#include "BasicArray.h"
#include "BasicMath.h"
#include "BasicString.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

void RWLock::AcquireExclusive() {
	AcquireSRWLockExclusive((PSRWLOCK)this);
}

void RWLock::ReleaseExclusive() {
	ReleaseSRWLockExclusive((PSRWLOCK)this);
}

static_assert(sizeof(RWLock) == sizeof(SRWLOCK), "Mismatching SRWLOCK size.");


compile_const u32 thread_pool_max_thread_count = 32;

struct alignas(64) ThreadPoolWorkItem {
	u64 begin_index = 0;
	u64 end_index   = 0;
	u64 batch_size  = 0;
	u64 worker_count = 0;
	
	ThreadPoolCallback callback = nullptr;
	void* user_data = nullptr;
};

struct ThreadPoolMainUserData {
	ThreadPool* thread_pool = nullptr;
	u32 thread_index = 0;
};

struct ThreadPool {
	alignas(64) bool thread_pool_active = true;
	alignas(64) CONDITION_VARIABLE condition_variable = CONDITION_VARIABLE_INIT;
	alignas(64) RWLock work_item_lock;
	alignas(64) ThreadPoolWorkItem work_item;
	
	FixedCapacityArray<HANDLE, thread_pool_max_thread_count> threads;
	FixedCapacityArray<ThreadPoolMainUserData, thread_pool_max_thread_count> thread_user_data;
};

static DWORD ThreadPoolMain(void* user_data);

ThreadPool* CreateThreadPool(StackAllocator* alloc) {
	auto* thread_pool = NewFromAlloc(alloc, ThreadPool);
	
	TempAllocationScope(alloc);
	
	SYSTEM_INFO system_info = {};
	GetSystemInfo(&system_info);
	
	u32 thread_count = Math::Min(Math::Max(system_info.dwNumberOfProcessors, 1u), thread_pool_max_thread_count);
	
	for (u32 i = 0; i < thread_count; i += 1) {
		ArrayAppend(thread_pool->thread_user_data, { thread_pool, i });
	}
	
	ArrayAppend(thread_pool->threads, GetCurrentThread());
	SetThreadDescription(GetCurrentThread(), L"0: Main");
	
	for (u32 i = 1; i < thread_count; i += 1) {
		auto handle = CreateThread(nullptr, 0, &ThreadPoolMain, (void*)&thread_pool->thread_user_data[i], 0, nullptr);
		
		SetThreadDescription(handle, (wchar_t*)StringUtf8ToUtf16(alloc, StringFormat(alloc, "%: Worker"_sl, i)).data);
		ArrayAppend(thread_pool->threads, handle);
	}
	return thread_pool;
}

void ReleaseThreadPool(ThreadPool* thread_pool) {
	{
		ScopedLock(thread_pool->work_item_lock);
		thread_pool->thread_pool_active = false;
	}
	
	WakeAllConditionVariable(&thread_pool->condition_variable);
	
	for (u32 i = 1; i < thread_pool->threads.count; i += 1) {
		WaitForSingleObject(thread_pool->threads[i], INFINITE);
		CloseHandle(thread_pool->threads[i]);
	}
}

static DWORD ThreadPoolMain(void* user_data) {
	auto* thread_pool_main_user_data = (ThreadPoolMainUserData*)user_data;
	auto* thread_pool = thread_pool_main_user_data->thread_pool;
	u32  thread_index = thread_pool_main_user_data->thread_index;
	
	while (thread_pool->thread_pool_active) {
		ThreadPoolWorkItem work_item;
		{
			ScopedLock(thread_pool->work_item_lock);
			
			if (thread_pool->work_item.begin_index < thread_pool->work_item.end_index) {
				work_item = thread_pool->work_item;
				thread_pool->work_item.begin_index  = work_item.begin_index + work_item.batch_size;
				thread_pool->work_item.worker_count += 1;
			} else {
				SleepConditionVariableSRW(&thread_pool->condition_variable, (PSRWLOCK)&thread_pool->work_item_lock, INFINITE, 0);
			}
		}
		
		if (work_item.callback != nullptr) {
			work_item.end_index = Math::Min(work_item.end_index, work_item.begin_index + work_item.batch_size);
			
			for (u64 i = work_item.begin_index; i < work_item.end_index; i += 1) {
				work_item.callback(work_item.user_data, i, thread_index);
			}
			
			ScopedLock(thread_pool->work_item_lock);
			thread_pool->work_item.worker_count -= 1;
		}
	}
	
	return 0;
}


void ParallelForWithCallback(ThreadPool* thread_pool, u64 begin_index, u64 end_index, u64 batch_size, void* user_data, ThreadPoolCallback callback) {
	if (begin_index + batch_size >= end_index || thread_pool == nullptr) {
		for (u64 i = begin_index; i < end_index; i += 1) {
			callback(user_data, i, 0);
		}
		return;
	}
	
	{
		ThreadPoolWorkItem work_item;
		work_item.begin_index = begin_index + batch_size;
		work_item.end_index   = end_index;
		work_item.batch_size  = batch_size;
		work_item.callback    = callback;
		work_item.user_data   = user_data;
		work_item.worker_count = 0;
		thread_pool->work_item = work_item;
		
		WakeAllConditionVariable(&thread_pool->condition_variable);
	}
	
	// Always consume the first work item on the calling thread.
	end_index = Math::Min(end_index, begin_index + batch_size);
	for (u64 i = begin_index; i < end_index; i += 1) {
		callback(user_data, i, 0);
	}
	
	bool parallel_for_active = true;
	while (parallel_for_active) {
		ThreadPoolWorkItem work_item;
		{
			ScopedLock(thread_pool->work_item_lock);
			
			work_item = thread_pool->work_item;
			thread_pool->work_item.begin_index = work_item.begin_index + work_item.batch_size;
		}
		
		work_item.end_index = Math::Min(work_item.end_index, work_item.begin_index + work_item.batch_size);
		
		for (u64 i = work_item.begin_index; i < work_item.end_index; i += 1) {
			work_item.callback(work_item.user_data, i, 0);
		}
		
		if (work_item.begin_index >= work_item.end_index && work_item.worker_count == 0) {
			parallel_for_active = false;
		}
	}
}
