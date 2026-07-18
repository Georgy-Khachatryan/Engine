#include "RenderPassArray.h"
#include "GraphicsApi/RecordContext.h"

static RenderPassSubmitRange* GetQueueSubmitRange(StackAllocator* alloc, Array<SubmitRangeID>& submit_range_order, RenderPassQueueArray& queue, CommandQueueType queue_type, bool create_new_submit_range = false) {
	if (queue.submit_ranges.count == 0 || create_new_submit_range) {
		SubmitRangeID submit_id;
		submit_id.submit_range_index = (u32)queue.submit_ranges.count;
		submit_id.queue_type         = queue_type;
		ArrayAppend(submit_range_order, alloc, submit_id);
		
		auto& range = ArrayEmplace(queue.submit_ranges, alloc);
		range.begin_render_pass_index = (u32)queue.render_passes.count;
		range.end_render_pass_index   = (u32)queue.render_passes.count;
	}
	
	return &ArrayLastElement(queue.submit_ranges);
}

void RenderPassArray::PushQueue(CommandQueueType new_queue_type) {
	if (enable_async_compute == false) return;
	ArrayAppend(queue_type_stack, current_queue_type);
	current_queue_type = new_queue_type;
}

void RenderPassArray::PopQueue() {
	if (enable_async_compute == false) return;
	current_queue_type = ArrayPopLast(queue_type_stack);
}

void RenderPassArray::AddPass(const RenderPassArrayEntry& entry) {
	auto& queue = queues[(u32)current_queue_type];
	auto* range = GetQueueSubmitRange(alloc, submit_range_order, queue, current_queue_type);
	if (range->signal_index != 0) {
		range = GetQueueSubmitRange(alloc, submit_range_order, queue, current_queue_type, true);
	}
	
	ArrayAppend(queue.render_passes, alloc, entry);
	range->end_render_pass_index = (u32)queue.render_passes.count;
}

QueueSubmitIndex RenderPassArray::AddSignal(CommandQueueType queue_type) {
	if (enable_async_compute == false) return {};
	
	auto& queue = queues[(u32)queue_type];
	auto* range = GetQueueSubmitRange(alloc, submit_range_order, queue, queue_type);
	
	range->signal_index = EncodeQueueSubmitIndex(frame_index, queue.submit_ranges.count - 1);
	
	return { range->signal_index, queue_type };
}

void RenderPassArray::AddWait(CommandQueueType queue_type, QueueSubmitIndex submit_index) {
	if (enable_async_compute == false) return;
	
	DebugAssert(queue_type != submit_index.queue_type, "Trying to wait for a signal from the same queue. Queue index: '%'", (u32)queue_type);
	
	auto& queue = queues[(u32)queue_type];
	auto* range = GetQueueSubmitRange(alloc, submit_range_order, queue, queue_type);
	if (range->signal_index != 0 || range->begin_render_pass_index != range->end_render_pass_index) {
		range = GetQueueSubmitRange(alloc, submit_range_order, queue, queue_type, true);
	}
	
	range->wait_indices[(u32)submit_index.queue_type] = Math::Max(range->wait_indices[(u32)submit_index.queue_type], submit_index.submit_index);
}

void ReplayRenderPasses(RenderPassArray& array, RecordContext* record_context) {
	for (auto submit_id : array.submit_range_order) {
		auto& queue = array.queues[(u32)submit_id.queue_type];
		auto& range = queue.submit_ranges[submit_id.submit_range_index];
		
		for (auto& entry : ArrayViewCreate(queue.render_passes, range.begin_render_pass_index, range.end_render_pass_index)) {
			CmdProfilerBeginScope(record_context, entry.debug_name);
			entry.record_pass(entry.render_pass, record_context);
			CmdProfilerEndScope(record_context);
		}
		
		QueueCmdSubmit(record_context, submit_id.queue_type, range.wait_indices, range.signal_index);
	}
}
