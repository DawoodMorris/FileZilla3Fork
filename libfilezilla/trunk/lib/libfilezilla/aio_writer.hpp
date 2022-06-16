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
	static constexpr auto nosize = static_cast<uint64_t>(-1);

	writer_base(writer_base const&) = delete;
	writer_base& operator=(writer_base const&) = delete;

	virtual aio_result preallocate(uint64_t /*size*/) { return aio_result::ok; }

	virtual bool offsettable() const { return false; }

	virtual aio_result add_buffer(buffer_lease && b, aio_waiter & h) = 0;
	virtual aio_result finalize(aio_waiter & h) = 0;

	void close();

protected:
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

	virtual void do_close(fz::scoped_lock &) {}

	mutex mtx_;
	aio_buffer_pool & buffer_pool_;

	std::wstring const name_;

	size_t const max_buffers_{};
	std::list<buffer_lease> buffers_;

	bool error_{};
	uint8_t finalizing_{};
};

class thread_pool;
class FZ_PUBLIC_SYMBOL threaded_writer : public writer_base
{
public:
	using writer_base::writer_base;

	void wakeup(scoped_lock & l) {
		cond_.signal(l);
	}

	virtual aio_result add_buffer(buffer_lease && b, aio_waiter & h) override;
	virtual aio_result finalize(aio_waiter & h) override;

protected:
	virtual void do_close(fz::scoped_lock & l) override;

	virtual aio_result continue_finalize(aio_waiter &, fz::scoped_lock &) {
		return aio_result::ok;
	}

	condition cond_;
	async_task task_;

	bool quit_{};
};

class FZ_PUBLIC_SYMBOL file_writer final : public threaded_writer
{
public:
	file_writer(std::wstring && name, aio_buffer_pool & pool, file && f, thread_pool & tpool, size_t offset = 0, size_t max_buffers = 4) noexcept;
	file_writer(std::wstring_view name, aio_buffer_pool & pool, file && f, thread_pool & tpool, size_t offset = 0, size_t max_buffers = 4) noexcept;

	virtual ~file_writer() override;

	virtual bool offsettable() const override { return true; }

	virtual aio_result preallocate(uint64_t size) override;

protected:
	virtual void do_close(fz::scoped_lock & l) override;
	virtual aio_result continue_finalize(aio_waiter & h, fz::scoped_lock & l) override;

private:

	void entry();

	file file_;

	bool fsync_{};
	bool preallocated_{};
	bool from_beginning_{};
};
}

#endif
