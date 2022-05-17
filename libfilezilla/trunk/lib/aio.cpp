#include "libfilezilla/aio.hpp"
#include "libfilezilla/util.hpp"

#ifdef FZ_WINDOWS
#include "libfilezilla/glue/windows.hpp"
#else
#include <sys/mman.h>
#include <unistd.h>
#endif


namespace {
size_t get_page_size()
{
#if FZ_WINDOWS
	static size_t const page_size = []() { SYSTEM_INFO i{}; GetSystemInfo(&i); return i.dwPageSize; }();
#else
	static size_t const page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
	return page_size;
}
}

namespace fz {

buffer_lease::buffer_lease(buffer_lease && op) noexcept
{
	pool_ = op.pool_;
	op.pool_ = nullptr;
	buffer_ = std::move(op.buffer_);
}

buffer_lease& buffer_lease::operator=(buffer_lease && op) noexcept
{
	if (this != &op) {
		release();
		pool_ = op.pool_;
		op.pool_ = nullptr;
		buffer_ = std::move(op.buffer_);
	}
	return *this;
}

void buffer_lease::release()
{
	if (pool_) {
		pool_->release(std::move(buffer_));
	}
}



void aio_waitable::add_waiter(aio_waiter & h)
{
	waiting_.emplace_back(&h);
}

void aio_waitable::remove_waiter(aio_waiter & h)
{
	scoped_lock l(m_);
	while (active_signalling_ == &h) {
		l.unlock();
		yield();
		l.lock();
	}
	std::remove(waiting_.begin(), waiting_.end(), &h);
}

void aio_waitable::remove_waiters()
{
	scoped_lock l(m_);
	while (active_signalling_) {
		l.unlock();
		yield();
		l.lock();
	}
	waiting_.clear();
}

void aio_waitable::signal_availibility()
{
	scoped_lock l(m_);
	if (!waiting_.empty()) {
		active_signalling_ = waiting_.back();
		waiting_.pop_back();
		l.unlock();
		active_signalling_->on_buffer_avilibility();
		l.lock();
		active_signalling_ = nullptr;
	}
}


aio_buffer_pool::aio_buffer_pool(size_t buffer_count, size_t buffer_size)
{
	if (!buffer_size) {
		buffer_size = 256*1024;
	}
	size_t const psz = get_page_size();

	// Get size per buffer, rounded up to page size
	size_t adjusted_buffer_size = buffer_size;
	if (adjusted_buffer_size % psz) {
		adjusted_buffer_size += psz - (adjusted_buffer_size % psz);
	}

	// Since different threads/processes operate on different buffers at the same time
	// seperate them with a padding page to prevent false sharing due to automatic prefetching.
	size_t const memory_size = (adjusted_buffer_size + psz) * buffer_count + psz;

	memory_ = new(std::nothrow) uint8_t[memory_size];
	if (memory_) {
		buffers_.reserve(buffer_count);
		auto *p = memory_ + psz;
		for (size_t i = 0; i < buffer_count; ++i, p += adjusted_buffer_size + psz) {
			buffers_.emplace_back(p, buffer_size);
		}
	}
}

aio_buffer_pool::~aio_buffer_pool()
{
	delete [] memory_;
}

buffer_lease aio_buffer_pool::get_buffer(aio_waiter & h)
{
	buffer_lease ret;

	scoped_lock l(mtx_);
	if (buffers_.empty()) {
		add_waiter(h);
	}
	else {
		ret = buffer_lease(buffers_.back(), this);
		buffers_.pop_back();
	}
	return ret;
}

void aio_buffer_pool::release(nonowning_buffer && b)
{
	scoped_lock l(mtx_);
	auto p = b.get();
	if (p) {
		b.clear();
		buffers_.emplace_back(b);
	}

	signal_availibility();
}

}
