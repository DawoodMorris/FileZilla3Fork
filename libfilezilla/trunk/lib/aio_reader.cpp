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

bool reader_base::rewind()
{
	return seek(start_offset_, size_);
}

bool reader_base::seek(uint64_t offset, uint64_t size)
{
	// Step 1: Sanity checks, ignore seekable() for now

	if (offset == nosize) {
		offset = (start_offset_ == nosize) ? 0 : start_offset_;
		if (size == nosize) {
			size = size_;
		}
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
		// Cannot start again if we already started once. Neither can we start from not the beginning.
		if (start_offset_ != nosize || offset != 0) {
			return false;
		}
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
	eof_ = remaining_ == 0;
	get_buffer_called_ = false;

	return do_seek(l);
}



std::pair<aio_result, buffer_lease> threaded_reader::get_buffer(aio_waiter & h)
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
		buffers_.pop_front();
		if (w) {
			wakeup(l);
		}
		get_buffer_called_ = true;
		return {aio_result::ok, std::move(b)};
	}
}


file_reader::file_reader(std::wstring && name, aio_buffer_pool & pool, file && f, thread_pool & tpool, uint64_t offset, uint64_t size, size_t max_buffers) noexcept
	: threaded_reader(name, pool, max_buffers)
    , file_(std::move(f))
    , thread_pool_(tpool)
{
	scoped_lock l(mtx_);
	if (file_) {
		auto s = file_.size();
		if (s >= 0) {
			max_size_ = static_cast<uint64_t>(s);
		}
		if (!seek(offset, size)) {
			error_ = true;
		}
	}
	else {
		error_ = true;
	}
}

file_reader::file_reader(std::wstring_view name, aio_buffer_pool & pool, file && f, thread_pool & tpool, uint64_t offset, uint64_t size, size_t max_buffers) noexcept
	: threaded_reader(name, pool, max_buffers)
    , file_(std::move(f))
    , thread_pool_(tpool)
{
	scoped_lock l(mtx_);
	if (file_) {
		auto s = file_.size();
		if (s >= 0) {
			max_size_ = static_cast<uint64_t>(s);
		}
		if (s >= 0) {
			max_size_ = static_cast<uint64_t>(s);
		}
		if (!seek(offset, size)) {
			error_ = true;
		}
	}
	if (!file_ || !task_) {
		error_ = true;
	}
}

file_reader::~file_reader() noexcept
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

	// Re-start thread if needed
	if (!eof_) {
		task_ = thread_pool_.spawn([this]{ entry(); });
		return task_.operator bool();
	}
	else {
		return true;
	}
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
				if (remaining_ && remaining_ != nosize) {
					error_ = true;
				}
				else {
					eof_ = true;
				}
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
		if ((eof_ || error_) && !quit_ && buffers_.empty()) {
			signal_availibility();
			break;
		}
	}
}


memory_reader::memory_reader(std::wstring && name, aio_buffer_pool & pool, std::string_view data) noexcept
	: reader_base(name, pool, 1)
	, data_(data)
{
	size_ = max_size_ = remaining_ = data_.size();
	if (!remaining_) {
		eof_ = true;
	}
}

memory_reader::~memory_reader() noexcept
{
	close();
}

void memory_reader::do_close(scoped_lock &)
{
}

std::pair<aio_result, buffer_lease> memory_reader::get_buffer(aio_waiter & h)
{
	if (error_) {
		return {aio_result::error, buffer_lease()};
	}
	else if (eof_) {
		return {aio_result::ok, buffer_lease()};
	}

	auto b = buffer_pool_.get_buffer(*this);
	if (!b) {
		add_waiter(h);
		return {aio_result::wait, buffer_lease()};
	}

	size_t to_read = b->capacity();
	if (remaining_ != nosize && remaining_ < to_read) {
		to_read = remaining_;
	}
	b->append(reinterpret_cast<uint8_t const*>(data_.data()) + start_offset_ + size_ - remaining_, to_read);
	remaining_ -= to_read;
	if (!remaining_) {
		eof_ = true;
	}
	get_buffer_called_ = true;

	return {aio_result::ok, std::move(b)};
}

void memory_reader::on_buffer_avilibility()
{
	signal_availibility();
}

bool memory_reader::do_seek(scoped_lock &)
{
	return true;
}

}
