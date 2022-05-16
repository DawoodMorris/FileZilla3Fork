#include "libfilezilla/aio_reader.hpp"

namespace fz {

void reader_base::close()
{
	scoped_lock l(mtx_);
	do_close(l);
	buffer_pool_.remove_waiter(*this);
	remove_waiters();
	buffers_.clear();
}

bool reader_base::seek(uint64_t offset, uint64_t size)
{
	// Step 1: Sanity checks, ignore seekable() for now

	if (offset == nosize) {
		offset = start_offset_;
	}

	if (size != nosize && nosize - size <= offset) {
		// offset + size overflow or nosize
		return false;
	}
	if (size != nosize && offset + size > max_size_) {
		// Range unfulfillable
		return false;
	}

	scoped_lock l(mtx_);
	if (error_) {
		return false;
	}

	// Step 2: Check if anything is actually changing, so that we don't have to throw buffers away

	bool change{};
	if (get_buffer_called_) {
		change = true;
	}

	if (offset != start_offset_) {
		change = true;
	}

	if (size == nosize) {
		if (offset + size_ != max_size_) {
			// We had a size restriction, now we have none.
			change = true;
		}
	}
	else {
		if (size != size_) {
			change = true;
		}
	}

	if (!change) {
		// No need to throw away buffers
		return true;
	}

	if (!seekable()) {
		return false;
	}

	buffer_pool_.remove_waiter(*this);
	remove_waiters();
	buffers_.clear();

	// Set the offset and sizes
	start_offset_ = offset;
	if (size != nosize) {
		size_ = size;
	}
	else {
		size_ = max_size_;
		if (size_ != nosize) {
			size_ -= start_offset_;
		}
	}
	remaining_ = size_;
	eof_ = false;
	get_buffer_called_ = false;

	return do_seek(l);
}

std::pair<aio_result, buffer_lease> reader_base::get_buffer(aio_waiter & h)
{
	scoped_lock l(mtx_);

	if (buffers_.empty()) {
		if (error_) {
			return {aio_result::error, buffer_lease()};
		}
		else if (eof_) {
			return {aio_result::ok, buffer_lease()};
		}
		add_waiter(h);
		return {aio_result::wait, buffer_lease()};
	}
	else {
		bool const w = buffers_.size() == max_buffers_;
		buffer_lease b = std::move(buffers_.front());
		buffers_.erase(buffers_.begin());
		if (w) {
			wakeup(l);
		}
		return {aio_result::ok, std::move(b)};
	}
}


file_reader::file_reader(thread_pool & tpool, file && f, std::wstring && name, aio_buffer_pool & pool, size_t max_buffers) noexcept
    : reader_base(name, pool, max_buffers)
    , file_(std::move(f))
    , thread_pool_(tpool)
{
	scoped_lock l(mtx_);
	if (file_) {
		auto s = file_.size();
		if (s > 0) {
			size_ = s;
			max_size_ = s;
		}
		task_ = tpool.spawn([this]{ entry(); });
	}
	if (!file_ || !task_) {
		error_ = true;
	}
}

file_reader::file_reader(thread_pool & tpool, file && f, std::wstring_view name, aio_buffer_pool & pool, size_t max_buffers) noexcept
    : reader_base(name, pool, max_buffers)
    , file_(std::move(f))
    , thread_pool_(tpool)
{
	scoped_lock l(mtx_);
	if (file_) {
		auto s = file_.size();
		if (s > 0) {
			size_ = s;
			max_size_ = s;
		}
		task_ = tpool.spawn([this]{ entry(); });
	}
	if (!file_ || !task_) {
		error_ = true;
	}
}

file_reader::~file_reader()
{
	close();
}

bool file_reader::seekable() const
{
	return max_size_ != nosize;
}

void file_reader::do_close(scoped_lock & l)
{
	quit_ = true;
	cond_.signal(l);
	l.unlock();
	task_.join();
	l.lock();
	file_.close();
}

bool file_reader::do_seek(scoped_lock & l)
{
	// Step 1: Stop thread
	quit_ = true;
	cond_.signal(l);
	l.unlock();
	task_.join();
	l.lock();
	quit_ = false;

	// Step 2
	if (file_.seek(start_offset_, file::begin) != static_cast<int64_t>(start_offset_)) {
		return false;
	}

	// Re-start thread
	task_ = thread_pool_.spawn([this]{ entry(); });
	return task_.operator bool();
}

void file_reader::wakeup(scoped_lock & l)
{
	cond_.signal(l);
}

void file_reader::on_buffer_avilibility()
{
	scoped_lock l(mtx_);
	cond_.signal(l);
}

void file_reader::entry()
{
	scoped_lock l(mtx_);
	while (!quit_ && !error_) {
		if (buffers_.size() == max_buffers_) {
			cond_.wait(l);
			continue;
		}
		auto b = buffer_pool_.get_buffer(*this);
		if (!b) {
			cond_.wait(l);
			continue;
		}
		while (b->size() < b->capacity()) {
			l.unlock();
			size_t to_read = b->capacity() - b->size();
			if (remaining_ != nosize && to_read > remaining_) {
				to_read = remaining_;
			}
			int64_t r = to_read ? file_.read(b->get(to_read), to_read) : 0;
			l.lock();
			if (quit_ || error_) {
				return;
			}
			if (r < 0) {
				error_ = true;
				break;
			}
			else if (!r) {
				eof_ = true;
				break;
			}
			b->add(r);
			if (remaining_ != nosize) {
				remaining_ -= r;
			}
		}

		if (!b->empty()) {
			buffers_.emplace_back(std::move(b));
			if (buffers_.size() == 1) {
				signal_availibility();
			}
		}
		if (eof_) {
			if (buffers_.empty()) {
				signal_availibility();
			}
			break;
		}
	}
}

}
