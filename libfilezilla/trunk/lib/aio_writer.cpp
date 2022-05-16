#include "libfilezilla/aio_writer.hpp"

namespace fz {
aio_result writer_base::add_buffer(buffer_lease && b, aio_waiter & h)
{
	scoped_lock l(mtx_);

	if (error_) {
		return aio_result::error;
	}

	if (!*b) {
		return aio_result::ok;
	}
	buffers_.emplace_back(std::move(b));

	if (buffers_.size() == 1) {
		wakeup(l);
	}

	if (buffers_.size() >= max_buffers_) {
		add_waiter(h);
		return aio_result::wait;
	}
	else {
		return aio_result::ok;
	}
}

aio_result writer_base::finalize(aio_waiter & h)
{
	scoped_lock l(mtx_);
	if (error_) {
		return aio_result::error;
	}
	if (buffers_.empty()) {
		return aio_result::ok;
	}
	finalize_ = true;
	add_waiter(h);
	return aio_result::wait;
}


file_writer::file_writer(thread_pool & tpool, file && f, std::wstring && name, aio_buffer_pool & pool, size_t max_buffers) noexcept
    : writer_base(name, pool, max_buffers)
    , file_(std::move(f))
{
	scoped_lock l(mtx_);
	if (file_) {
		task_ = tpool.spawn([this]{ entry(); });
	}
	if (!file_ || !task_) {
		error_ = true;
	}
}

file_writer::file_writer(thread_pool & tpool, file && f, std::wstring_view name, aio_buffer_pool & pool, size_t max_buffers) noexcept
    : writer_base(name, pool, max_buffers)
    , file_(std::move(f))
{
	scoped_lock l(mtx_);
	if (file_) {
		task_ = tpool.spawn([this]{ entry(); });
	}
	if (!file_ || !task_) {
		error_ = true;
	}
}

file_writer::~file_writer()
{
	{
		scoped_lock l(mtx_);
		quit_ = true;
		cond_.signal(l);
	}
	task_.join();
	remove_waiters();
}

void file_writer::wakeup(scoped_lock & l)
{
	cond_.signal(l);
}

void file_writer::entry()
{
	scoped_lock l(mtx_);
	while (!quit_ && !error_) {
		if (buffers_.empty()) {
			if (finalize_) {
				signal_availibility();
				break;
			}
			cond_.wait(l);
			continue;
		}
		auto & b = buffers_.front();
		while (!b->empty()) {
			l.unlock();
			int64_t written = file_.write(b->get(), b->size());
			l.lock();
			if (quit_ || error_) {
				return;
			}
			if (written <= 0) {
				error_ = true;
				return;
			}
			b->consume(static_cast<size_t>(written));
		}
		bool const signal = buffers_.size() == max_buffers_;
		buffers_.erase(buffers_.begin());
		if (signal) {
			signal_availibility();
		}
	}
}
}
