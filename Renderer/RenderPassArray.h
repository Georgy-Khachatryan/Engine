#pragma once
#include "Basic/Basic.h"
#include "GraphicsApi/GraphicsApiTypes.h"

using RecordPassCallback = void(*)(void*, RecordContext*);

struct RenderPassArrayEntry {
	RecordPassCallback record_pass = nullptr;
	void* render_pass = nullptr;
	String debug_name = "Unknown"_sl;
};

struct RenderPassSubmitRange {
	FixedCountArray<u64, (u32)CommandQueueType::Count> wait_indices = {};
	u64 signal_index = 0;
	
	u32 begin_render_pass_index = 0;
	u32 end_render_pass_index   = 0;
};

struct SubmitRangeID {
	u32 submit_range_index = 0;
	CommandQueueType queue_type = CommandQueueType::Graphics;
};

struct RenderPassQueueArray {
	Array<RenderPassArrayEntry>  render_passes;
	Array<RenderPassSubmitRange> submit_ranges;
};

struct RenderPassArray {
	StackAllocator* alloc = nullptr;
	
	FixedCountArray<RenderPassQueueArray, (u32)CommandQueueType::Count> queues;
	Array<SubmitRangeID> submit_range_order;
	
	FixedCapacityArray<CommandQueueType, 8> queue_type_stack;
	u64 frame_index = 0;
	
	CommandQueueType current_queue_type = CommandQueueType::Graphics;
	bool enable_async_compute = false;
	
	template<typename RenderPassT>
	RenderPassT& Add(String debug_name_substitution = ""_sl) {
		auto* render_pass = NewFromAlloc(alloc, RenderPassT);
		
		RenderPassArrayEntry entry;
		entry.render_pass = render_pass;
		entry.record_pass = [](void* render_pass, RecordContext* record_context) { return ((RenderPassT*)render_pass)->RecordPass(record_context); };
		entry.debug_name  = debug_name_substitution.count != 0 ? debug_name_substitution : RenderPassT::debug_name;
		AddPass(entry);
		
		return *render_pass;
	}
	
	void PushQueue(CommandQueueType new_queue_type);
	void PopQueue();
	QueueSubmitIndex AddSignal(CommandQueueType queue_type);
	void AddWait(CommandQueueType queue_type, QueueSubmitIndex submit_index);
	void AddPass(const RenderPassArrayEntry& entry);
};

void ReplayRenderPasses(RenderPassArray& array, RecordContext* record_context);
