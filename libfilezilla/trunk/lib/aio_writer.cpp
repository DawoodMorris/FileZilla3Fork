#include "libfilezilla/aio_writer.hpp"
#include "libfilezilla/local_filesys.hpp"
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

writer_factory_holder::writer_factory_holder(writer_factory_holder const& op)
{
	if (op.impl_) {
		impl_ = op.impl_->clone();
	}
}

writer_factory_holder& writer_factory_holder::operator=(writer_factory_holder const& op)
{
	if (this != &op && op.impl_) {
		impl_ = op.impl_->clone();
	}
	return *this;
}

writer_factory_holder::writer_factory_holder(writer_factory_holder && op) noexcept
{
	impl_ = std::move(op.impl_);
	op.impl_.reset();
}

writer_factory_holder& writer_factory_holder::operator=(writer_factory_holder && op) noexcept
{
	if (this != &op) {
		impl_ = std::move(op.impl_);
		op.impl_.reset();
	}

	return *this;
}

writer_factory_holder::writer_factory_holder(std::unique_ptr<writer_factory> && factory)
	: impl_(std::move(factory))
{
}

writer_factory_holder::writer_factory_holder(std::unique_ptr<writer_factory> const& factory)
	: impl_(factory ? factory->clone() : nullptr)
{
}

writer_factory_holder& writer_factory_holder::operator=(std::unique_ptr<writer_factory> && factory)
{
	if (impl_ != factory) {
		impl_ = std::move(factory);
	}

	return *this;
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

file_writer::file_writer(std::wstring && name, aio_buffer_pool & pool, file && f, thread_pool & tpool, bool fsync, progress_cb_t && progress_cb, size_t max_buffers) noexcept
	: threaded_writer(name, pool, std::move(progress_cb), max_buffers)
    , file_(std::move(f))
	, fsync_(fsync)
{
	if (file_) {
		task_ = tpool.spawn([this]{ entry(); });
	}
	if (!file_ || !task_) {
		file_.close();
		error_ = true;
	}
}

file_writer::file_writer(std::wstring_view name, aio_buffer_pool & pool, file && f, thread_pool & tpool, bool fsync, progress_cb_t && progress_cb, size_t max_buffers) noexcept
	: threaded_writer(name, pool, std::move(progress_cb), max_buffers)
    , file_(std::move(f))
	, fsync_(fsync)
{
	if (file_) {
		task_ = tpool.spawn([this]{ entry(); });
	}
	if (!file_ || !task_) {
		file_.close();
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
		if (!finalizing_&& !file_.position()) {
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
			if (progress_cb_) {
				progress_cb_(this, static_cast<uint64_t>(written));
			}
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
	fz::scoped_lock l(mtx_);
	if (error_ || !buffers_.empty() || finalizing_) {
		return aio_result::error;
	}

	buffer_pool_.logger().log(logmsg::debug_info, L"Preallocating %d bytes for the file \"%s\"", size, name_);

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

bool file_writer::set_mtime(fz::datetime const& t)
{
	fz::scoped_lock l(mtx_);
	if (error_ || finalizing_ != 2 || !file_) {
		return false;
	}

	return file_.set_modification_time(t);
}

file_writer_factory::file_writer_factory(std::wstring const& file, thread_pool & tpool, file_writer_flags flags)
	: writer_factory(file)
	, thread_pool_(tpool)
	, flags_(flags)
{
}

std::unique_ptr<writer_base> file_writer_factory::open(aio_buffer_pool & pool, uint64_t offset, writer_base::progress_cb_t progress_cb, size_t max_buffers)
{
	if (!max_buffers) {
		max_buffers = preferred_buffer_count();
	}

	fz::file::creation_flags flags = offset ? fz::file::existing : fz::file::empty;
	if (flags_ & file_writer_flags::permissions_current_user_only) {
		flags |= fz::file::current_user_only;
	}
	else if (flags_ & file_writer_flags::permissions_current_user_and_admins_only) {
		flags |= fz::file::current_user_and_admins_only;
	}
	auto f = fz::file(fz::to_native(name()), fz::file::writing, flags);
	if (!f) {
		return {};
	}

	if (offset) {
		auto seek = static_cast<int64_t>(offset);
		auto new_pos = f.seek(seek, fz::file::begin);
		if (new_pos != seek) {
			pool.logger().log(logmsg::error, fztranslate("Could not seek to offset %d within '%s'."), seek, name());
			return {};
		}
		if (!f.truncate()) {
			pool.logger().log(logmsg::error, fztranslate("Could not truncate '%s' to offset %d."), name(), offset);
			return {};
		}
	}

	return std::make_unique<file_writer>(name(), pool, std::move(f), thread_pool_, flags_ & file_writer_flags::fsync, std::move(progress_cb), max_buffers);
}

std::unique_ptr<writer_factory> file_writer_factory::clone() const
{
	return std::make_unique<file_writer_factory>(*this);
}

uint64_t file_writer_factory::size() const
{
	auto s = fz::local_filesys::get_size(fz::to_native(name()));
	if (s < 0) {
		return writer_base::nosize;
	}
	else {
		return static_cast<uint64_t>(s);
	}
}

fz::datetime file_writer_factory::mtime() const
{
	return fz::local_filesys::get_modification_time(fz::to_native(name()));
}


bool file_writer_factory::set_mtime(fz::datetime const& t)
{
	return fz::local_filesys::set_modification_time(fz::to_native(name()), t);
}

}
