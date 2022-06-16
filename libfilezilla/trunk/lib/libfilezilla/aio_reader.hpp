#ifndef LIBFILEZILLA_AIO_READER_HEADER
#define LIBFILEZILLA_AIO_READER_HEADER

#include "aio.hpp"
#include "file.hpp"
#include "thread_pool.hpp"

#include <list>

namespace fz {

class FZ_PUBLIC_SYMBOL reader_base : protected aio_waiter, public aio_waitable
{
public:
	static constexpr auto nosize = static_cast<uint64_t>(-1);

	reader_base(reader_base const&) = delete;
	reader_base& operator=(reader_base const&) = delete;

	void close();

	virtual bool seekable() const { return false; }

	/// If seek fails, the reader is in an undefined state and must be closed
	bool seek(uint64_t offset, uint64_t size = nosize);

	bool rewind();

	std::wstring const& name() const { return name_; }

	// A reader may have an indetermined size.
	virtual uint64_t size() const { return size_; }

	virtual datetime mtime() const { return {}; }

	virtual std::pair<aio_result, buffer_lease> get_buffer(aio_waiter & h) = 0;

protected:
	reader_base(std::wstring && name, aio_buffer_pool & pool, size_t max_buffers) noexcept
	    : buffer_pool_(pool)
	    , logger_(pool.logger())
	    , name_(name)
	    , max_buffers_(max_buffers)
	{}

	reader_base(std::wstring_view name, aio_buffer_pool & pool, size_t max_buffers) noexcept
	    : buffer_pool_(pool)
	    , logger_(pool.logger())
	    , name_(name)
	    , max_buffers_(max_buffers)
	{}

	/// When this gets called, buffers_ has already been cleared and the waiters have been removed.
	/// start_offset_, size_ and remaining_ have already been set.
	virtual bool do_seek(scoped_lock &) {
		return false;
	}

	virtual void do_close(scoped_lock &) {}

	mutex mtx_;
	aio_buffer_pool & buffer_pool_;
	logger_interface & logger_;

	std::wstring const name_;

	size_t const max_buffers_{};
	std::list<buffer_lease> buffers_;

	uint64_t size_{nosize};
	uint64_t max_size_{nosize};
	uint64_t start_offset_{nosize};
	uint64_t remaining_{nosize};

	bool get_buffer_called_{};
	bool error_{};
	bool eof_{};
};

class thread_pool;
class FZ_PUBLIC_SYMBOL threaded_reader : public reader_base
{
public:
	using reader_base::reader_base;
	virtual std::pair<aio_result, buffer_lease> get_buffer(aio_waiter & h) override;

protected:
	void wakeup(scoped_lock & l) {
		cond_.signal(l);
	}

	condition cond_;
	async_task task_;

	bool quit_{};
};


class FZ_PUBLIC_SYMBOL file_reader final : public threaded_reader
{
public:
	file_reader(std::wstring && name, aio_buffer_pool & pool, file && f, thread_pool & tpool, uint64_t offset = 0, uint64_t size = nosize, size_t max_buffers = 4) noexcept;
	file_reader(std::wstring_view name, aio_buffer_pool & pool, file && f, thread_pool & tpool, uint64_t offset = 0, uint64_t size = nosize, size_t max_buffers = 4) noexcept;

	virtual ~file_reader() noexcept;

	virtual bool seekable() const override;

private:
	virtual void do_close(scoped_lock & l) override;
	virtual bool do_seek(scoped_lock & l) override;

	virtual void on_buffer_avilibility() override;

	void entry();

	file file_;
	thread_pool & thread_pool_;
};

/// Does not own the data, uses just one buffer
class FZ_PUBLIC_SYMBOL memory_reader final : public reader_base
{
public:
	memory_reader(std::wstring && name, aio_buffer_pool & pool, std::string_view data) noexcept;

	virtual ~memory_reader() noexcept;

	virtual bool seekable() const override { return true; }

	virtual std::pair<aio_result, buffer_lease> get_buffer(aio_waiter & h) override;

private:
	virtual void do_close(scoped_lock & l) override;
	virtual bool do_seek(scoped_lock & l) override;

	virtual void on_buffer_avilibility() override;

	std::string_view const data_;
};

}
#endif
