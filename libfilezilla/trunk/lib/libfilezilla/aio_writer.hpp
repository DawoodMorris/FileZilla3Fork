#ifndef LIBFILEZILLA_AIO_WRITER_HEADER
#define LIBFILEZILLA_AIO_WRITER_HEADER

#include "aio.hpp"
#include "file.hpp"
#include "thread_pool.hpp"

#include <list>

namespace fz {
class FZ_PUBLIC_SYMBOL writer_base : public aio_waitable
{
public:
	writer_base(std::wstring && name, aio_buffer_pool & pool, size_t max_buffers) noexcept
	    : buffer_pool_(pool)
	    , name_(name)
	    , max_buffers_(max_buffers)
	{}

	writer_base(std::wstring_view name, aio_buffer_pool & pool, size_t max_buffers) noexcept
	    : buffer_pool_(pool)
	    , name_(name)
	    , max_buffers_(max_buffers)
	{}

	writer_base(writer_base const&) = delete;
	writer_base& operator=(writer_base const&) = delete;

	aio_result add_buffer(buffer_lease && b, aio_waiter & h);
	aio_result finalize(aio_waiter & h);

	virtual aio_result preallocate(uint64_t /*size*/) { return aio_result::ok; }

protected:
	virtual void wakeup(scoped_lock &) {}

	mutex mtx_;
	aio_buffer_pool & buffer_pool_;

	std::wstring const name_;

	size_t const max_buffers_{};
	std::list<buffer_lease> buffers_;

	bool error_{};
	bool finalize_{};
};


class FZ_PUBLIC_SYMBOL file_writer final : public writer_base
{
public:
	file_writer(thread_pool & tpool, file && f, std::wstring && name, aio_buffer_pool & pool, size_t max_buffers) noexcept;
	file_writer(thread_pool & tpool, file && f, std::wstring_view name, aio_buffer_pool & pool, size_t max_buffers) noexcept;

	virtual ~file_writer() override;

private:

	virtual void wakeup(scoped_lock & l) override;

	void entry();

	condition cond_;
	async_task task_;
	file file_;

	bool quit_{};
};
}

#endif
