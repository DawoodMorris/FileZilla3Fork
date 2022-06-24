#include "libfilezilla/aio.hpp"
#include "libfilezilla/logger.hpp"
#include "libfilezilla/util.hpp"

#ifdef FZ_WINDOWS
#include "libfilezilla/glue/windows.hpp"
#else
#include "libfilezilla/encode.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
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
		pool_ = nullptr;
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
		active_signalling_->on_buffer_availability();
		l.lock();
		active_signalling_ = nullptr;
	}
}


#if FZ_WINDOWS
aio_buffer_pool::shm_handle const aio_buffer_pool::shm_handle_default{INVALID_HANDLE_VALUE};
#endif

#if FZ_MAC
aio_buffer_pool::aio_buffer_pool(logger_interface & logger, size_t buffer_count, size_t buffer_size, bool use_shm, std::string_view application_group_id)
#else
aio_buffer_pool::aio_buffer_pool(logger_interface & logger, size_t buffer_count, size_t buffer_size, bool use_shm)
#endif
	: logger_{logger}
	, buffer_count_{buffer_count}
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
	memory_size_ = (adjusted_buffer_size + psz) * buffer_count + psz;

	if (use_shm) {
#if FZ_WINDOWS
		shm_ = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(memory_size_), nullptr);
		if (!shm_ || shm_ == INVALID_HANDLE_VALUE) {
			shm_ = INVALID_HANDLE_VALUE;
			DWORD err = GetLastError();
			logger_.log(logmsg::debug_warning, "CreateFileMapping failed with error %u", err);
			return;
		}
		memory_ = static_cast<uint8_t*>(MapViewOfFile(reinterpret_cast<HANDLE>(shm_), FILE_MAP_ALL_ACCESS, 0, 0, memory_size_));
		if (!memory_) {
			DWORD err = GetLastError();
			logger_.log(logmsg::debug_warning, "MapViewOfFile failed with error %u", err);
			return;
		}
#else
#if HAVE_MEMFD_CREATE
		shm_ = memfd_create("aio_buffer_pool", MFD_CLOEXEC|MFD_ALLOW_SEALING);
#else
		std::string name;
#if FZ_MAC
		if (!application_group_id.empty()) {
			name = std::string(application_group_id) + "/" + base32_encode(random_bytes(10), base32_type::locale_safe, false);
		}
		else
#endif
		{
			name = "/" + base32_encode(random_bytes(16), base32_type::locale_safe, false);
		}

		shm_ = shm_open(name.c_str(), O_CREAT|O_EXCL|O_RDWR, S_IRUSR|S_IWUSR);
		if (shm_ != -1) {
			shm_unlink(name.c_str());
		}
#endif
		if (shm_ == -1) {
			int err = errno;
			logger_.log(logmsg::debug_warning, L"Could not create shm_fd_, errno=%d", err);
			return;
		}

#if FZ_MAC
		// There's a bug on macOS: ftruncate can only be called _once_ on a shared memory object.
		// The manpages do not cover this bug, only XNU's bsd/kern/posix_shm.c mentions it.
		struct stat s;
		if (fstat(shm_, &s) != 0) {
			int err = errno;
			logger_.log(logmsg::debug_warning, "fstat failed with error %d", err);
			return;
		}

		if (s.st_size < 0 || static_cast<size_t>(s.st_size) < memory_size_)
#endif
		{
			if (ftruncate(shm_, memory_size_) != 0) {
				int err = errno;
				logger_.log(logmsg::debug_warning, "ftruncate failed with error %d", err);
				return;
			}
		}

#if HAVE_MEMFD_CREATE
		if (fcntl(shm_, F_ADD_SEALS, F_SEAL_SHRINK)) {
			int err = errno;
			logger_.log(logmsg::debug_warning, "sealing failed with error %d", err);
			return;
		}
#endif

		memory_ = static_cast<uint8_t*>(mmap(nullptr, memory_size_, PROT_READ|PROT_WRITE, MAP_SHARED, shm_, 0));
		if (!memory_) {
			int err = errno;
			logger_.log(logmsg::debug_warning, "mmap failed with error %d", err);
			return;
		}
#endif
	}
	else {
		memory_ = new(std::nothrow) uint8_t[memory_size_];
	}
	if (memory_) {
		buffers_.reserve(buffer_count);
		auto *p = memory_ + psz;
		for (size_t i = 0; i < buffer_count; ++i, p += adjusted_buffer_size + psz) {
			buffers_.emplace_back(p, buffer_size);
		}
	}
}

aio_buffer_pool::~aio_buffer_pool() noexcept
{
	scoped_lock l(mtx_);
	if (memory_ && buffers_.size() != buffer_count_) {
		abort();
	}
	if (shm_ != shm_handle_default) {
#if FZ_WINDOWS
		if (memory_) {
			UnmapViewOfFile(memory_);
		}
		CloseHandle(reinterpret_cast<HANDLE>(shm_));
#else
		if (memory_) {
			munmap(memory_, memory_size_);
		}
		close(shm_);
#endif
	}
	else {
		delete [] memory_;
	}
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

std::tuple<aio_buffer_pool::shm_handle, uint8_t const*, size_t> aio_buffer_pool::shared_memory_info() const
{
	scoped_lock l(mtx_);
	return {shm_, memory_, memory_size_};
}

}
