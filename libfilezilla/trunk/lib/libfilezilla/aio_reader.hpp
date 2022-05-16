#ifndef LIBFILEZILLA_AIO_READER_HEADER
#define LIBFILEZILLA_AIO_READER_HEADER

#include "aio.hpp"
#include "file.hpp"
#include "thread_pool.hpp"

namespace fz {

class FZ_PUBLIC_SYMBOL reader_base : protected aio_waiter, public aio_waitable
{
public:
	reader_base(reader_base const&) = delete;
	reader_base& operator=(reader_base const&) = delete;

	static constexpr auto nosize = static_cast<uint64_t>(-1);

	void close();

	virtual bool seekable() const { return false; }
	bool rewind() { return seek(0, nosize); }

	/// If seek fails, the reader is in an undefined state and must be closed
	bool seek(uint64_t offset, uint64_t size = nosize);

	std::wstring const& name() const { return name_; }

	// A reader may have an indetermined size.
	virtual uint64_t size() const { return size_; }

	virtual datetime mtime() const { return {}; }

	std::pair<aio_result, buffer_lease> get_buffer(aio_waiter & h);

protected:
	reader_base(std::wstring && name, aio_buffer_pool & pool, size_t max_buffers) noexcept
	    : buffer_pool_(pool)
	    , name_(name)
	    , max_buffers_(max_buffers)
	{}

	reader_base(std::wstring_view name, aio_buffer_pool & pool, size_t max_buffers) noexcept
	    : buffer_pool_(pool)
	    , name_(name)
	    , max_buffers_(max_buffers)
	{}

	/// When this gets called, buffers_ has already been cleared and the waiters have been removed.
	/// start_offset_, size_ and remaining_ have already been set.
	virtual bool do_seek(scoped_lock &) {
		return false;
	}

	virtual void do_close(scoped_lock &) {}

	virtual void wakeup(scoped_lock &) {}

	mutex mtx_;
	aio_buffer_pool & buffer_pool_;

	std::wstring const name_;

	size_t const max_buffers_{};
	std::vector<buffer_lease> buffers_;

	uint64_t size_{nosize};
	uint64_t max_size_{nosize};
	uint64_t start_offset_{0};
	uint64_t remaining_{nosize};

	bool get_buffer_called_{};
	bool error_{};
	bool eof_{};
};

class thread_pool;
class FZ_PUBLIC_SYMBOL file_reader final : public reader_base
{
public:
	file_reader(thread_pool & tpool, file && f, std::wstring && name, aio_buffer_pool & pool, size_t max_buffers) noexcept;
	file_reader(thread_pool & tpool, file && f, std::wstring_view name, aio_buffer_pool & pool, size_t max_buffers) noexcept;

	virtual ~file_reader() noexcept;

	virtual bool seekable() const override;

private:
	virtual void do_close(scoped_lock & l) override;
	virtual bool do_seek(scoped_lock & l) override;

	virtual void wakeup(scoped_lock & l) override;
	virtual void on_buffer_avilibility() override;

	void entry();

	file file_;
	thread_pool & thread_pool_;

	condition cond_;
	async_task task_;

	bool quit_{};
};

}
#endif
