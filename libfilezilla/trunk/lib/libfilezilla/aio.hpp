#ifndef LIBFILEZILLA_AIO_HEADER
#define LIBFILEZILLA_AIO_HEADER

#include "mutex.hpp"
#include "nonowning_buffer.hpp"

namespace fz {

// DECLS

class aio_buffer_pool;
class FZ_PUBLIC_SYMBOL buffer_lease final
{
public:
	constexpr buffer_lease() noexcept = default;
	~buffer_lease() noexcept
	{
		release();
	}

	buffer_lease(buffer_lease && op) noexcept;
	buffer_lease& operator=(buffer_lease && op) noexcept;

	buffer_lease(buffer_lease const&) = delete;
	buffer_lease& operator=(buffer_lease const&) = delete;

	explicit operator bool() const { return pool_ != nullptr; }

	// operator. would be nice
	nonowning_buffer const* operator->() const { return &buffer_; }
	nonowning_buffer* operator->() { return &buffer_; }
	nonowning_buffer const& operator*() const { return buffer_; }
	nonowning_buffer & operator*() { return buffer_; }

	void release();

	nonowning_buffer buffer_;
private:
	friend class aio_buffer_pool;
	buffer_lease(nonowning_buffer b, aio_buffer_pool* pool)
	    : buffer_(b)
	    , pool_(pool)
	{
	}

	aio_buffer_pool* pool_{};
};

class FZ_PUBLIC_SYMBOL aio_waiter
{
public:
	virtual ~aio_waiter() = default;

protected:
	// Will be invoked from unspecified thread. Only use it to signal the target thread.
	// In particular, never call into aio_buffer_pool from this function
	virtual void on_buffer_avilibility() = 0;

	friend class aio_waitable;
};

class FZ_PUBLIC_SYMBOL aio_waitable
{
public:
	virtual ~aio_waitable() = default;
	void remove_waiter(aio_waiter & h);

protected:

	/// Call in destructor of most-derived class
	void remove_waiters();

	void add_waiter(aio_waiter & h);
	void signal_availibility();

private:
	mutex m_;
	std::vector<aio_waiter*> waiting_;
	aio_waiter* active_signalling_{};
};

class logger_interface;

class FZ_PUBLIC_SYMBOL aio_buffer_pool final : public aio_waitable
{
public:
	// If buffer_size is 0, it picks a suitable default
#if FZ_MAC
	// On macOS, if using sandboxing, you need to pass an application group identifier.
	aio_buffer_pool(logger_interface & logger, size_t buffer_count = 1, size_t buffer_size = 0, bool use_shm = false, std::string_view application_group_id = {});
#else
	aio_buffer_pool(logger_interface & logger, size_t buffer_count = 1, size_t buffer_size = 0, bool use_shm = false);
#endif
	~aio_buffer_pool() noexcept;

	operator bool() const {
		return memory_ != nullptr;
	}

	// Wakeup order is unspecified.
	// If you depend on wakeup order, you are not using this class correctly.
	buffer_lease get_buffer(aio_waiter & h);

	logger_interface & logger() const { return logger_; }

#if FZ_WINDOWS
	// A HANDLE
	typedef void* shm_handle;
	static shm_handle const shm_handle_default;
#else
	// A file descriptor
	typedef int shm_handle;
	static shm_handle constexpr shm_handle_default{-1};
#endif

	std::tuple<shm_handle, uint8_t const*, size_t> shared_memory_info() const;

	size_t buffer_count() const { return buffer_count_; }

private:
	friend class buffer_lease;
	void release(nonowning_buffer && b);

	logger_interface & logger_;

	mutable mutex mtx_;

	uint64_t memory_size_{};
	uint8_t* memory_{};

	std::vector<nonowning_buffer> buffers_;

	shm_handle shm_{shm_handle_default};

	size_t const buffer_count_{};
};

enum class aio_result
{
	ok,
	wait,
	error
};

}

#endif
