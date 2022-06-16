#include "libfilezilla/aio_writer.hpp"
#include "libfilezilla/logger.hpp"
#include "libfilezilla/translate.hpp"

namespace fz {

void writer_base::close()
{
	scoped_lock l(mtx_);
	do_close(l);
	remove_waiters();
	buffers_.clear();
}

aio_result threaded_writer::add_buffer(buffer_lease && b, aio_waiter & h)
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

aio_result threaded_writer::finalize(aio_waiter & h)
{
	scoped_lock l(mtx_);
	if (error_) {
		return aio_result::error;
	}
	if (finalizing_ == 2) {
		return aio_result::ok;
	}
	finalizing_ = 1;
	return continue_finalize(h, l);
}

aio_result file_writer::continue_finalize(aio_waiter & h, fz::scoped_lock & l)
{
	if (!file_) {
		error_ = true;
		return aio_result::error;
	}

	if (fsync_ && buffers_.empty()) {
		wakeup(l);
	}
	if (!buffers_.empty() || fsync_) {
		add_waiter(h);
		return aio_result::wait;
	}
	return aio_result::ok;
}

void threaded_writer::do_close(fz::scoped_lock & l)
{
	quit_ = true;
	cond_.signal(l);
	l.unlock();
	task_.join();
	l.lock();
}

file_writer::file_writer(std::wstring && name, aio_buffer_pool & pool, file && f, thread_pool & tpool, size_t offset, size_t max_buffers) noexcept
	: threaded_writer(name, pool, max_buffers)
    , file_(std::move(f))
{
	from_beginning_ = offset == 0;

	scoped_lock l(mtx_);
	if (file_) {
		task_ = tpool.spawn([this]{ entry(); });
	}
	if (!file_ || !task_) {
		error_ = true;
	}
}

file_writer::file_writer(std::wstring_view name, aio_buffer_pool & pool, file && f, thread_pool & tpool, size_t offset, size_t max_buffers) noexcept
	: threaded_writer(name, pool, max_buffers)
    , file_(std::move(f))
{
	from_beginning_ = offset == 0;

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
	close();
}

void file_writer::do_close(fz::scoped_lock & l)
{
	threaded_writer::do_close(l);
	if (file_) {
		bool remove{};
		if (from_beginning_ && !file_.position() && !finalizing_) {
				// Freshly created file to which nothing has been written.
				remove = true;
		}
		else if (preallocated_) {
				// The file might have been preallocated and the writing stopped before being completed,
				// so always truncate the file before closing it regardless of finalize state.
				file_.truncate();
		}
		file_.close();

		if (remove) {
				buffer_pool_.logger().log(logmsg::debug_verbose, L"Deleting empty file '%s'", name_);
				fz::remove_file(fz::to_native(name_));
		}
		file_.close();
	}
}

void file_writer::entry()
{
	scoped_lock l(mtx_);
	while (!quit_ && !error_) {
		if (buffers_.empty()) {
			if (finalizing_ == 1) {
				finalizing_ = 2;
				if (fsync_) {
					if (!file_.fsync()) {
						buffer_pool_.logger().log(logmsg::error, fztranslate("Could not sync '%s' to disk."), name_);
						error_ = true;
					}
				}

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

aio_result file_writer::preallocate(uint64_t size)
{
	if (error_) {
		return aio_result::error;
	}

	buffer_pool_.logger().log(logmsg::debug_info, L"Preallocating %d bytes for the file \"%s\"", size, name_);

	fz::scoped_lock l(mtx_);

	auto oldPos = file_.seek(0, fz::file::current);
	if (oldPos < 0) {
		return aio_result::error;
	}

	auto seek_offet = static_cast<int64_t>(oldPos + size);
	if (file_.seek(seek_offet, fz::file::begin) == seek_offet) {
		if (!file_.truncate()) {
			buffer_pool_.logger().log(logmsg::debug_warning, L"Could not preallocate the file");
		}
	}
	if (file_.seek(oldPos, fz::file::begin) != oldPos) {
		buffer_pool_.logger().log(logmsg::error, fztranslate("Could not seek to offset %d within '%s'."), oldPos, name_);
		error_ = true;
		return aio_result::error;
	}
	preallocated_ = true;

	return aio_result::ok;
}

}
