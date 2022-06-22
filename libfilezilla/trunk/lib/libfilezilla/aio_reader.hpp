#ifndef LIBFILEZILLA_AIO_READER_HEADER
#define LIBFILEZILLA_AIO_READER_HEADER

#include "aio.hpp"
#include "file.hpp"
#include "thread_pool.hpp"

#include <list>

namespace fz {

/** \brief Base class for all readers
 *
 * All readers have a name describing them for logging purposes.
 *
 * The initial state of a freshly opened reader is readable, get_buffer() can be called.
 *
 * See the aio demo program for example usage.
 */
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

	/// Only seekable readers can be rewound.
	bool rewind();

	std::wstring const& name() const { return name_; }

	/// Size of the reader. If the size is indetermined, nosize is returned.
	virtual uint64_t size() const { return size_; }

	/// Last modification time, might be indetermined.
	virtual datetime mtime() const { return {}; }

	/** \brief Gets the next buffer with data from the reader.
	 *
	 * If it returns aio_result::ok, a buffer may be returned as well for the caller
	 * to consume. If no buffer is returned on aio_result::ok, the reader has reached eof.
	 *
	 * If aio_result::error is returned, the reader has failed and can only be closed.
	 *
	 * After getting aio_result::wait, do not call get_buffer again until after the passed
	 * waiter got on_buffer_availability() invoked.
	 */
	virtual std::pair<aio_result, buffer_lease> get_buffer(aio_waiter & h) = 0;

protected:
	/**
	 * \brief Constructs a reader.
	 *
	 * The passed \c aio_buffer_pool must live longer than the reader.
	 *
	 * \c max_buffers controls the amount of buffers the reader is prepared to
	 * use at any given time. \sa reader_factory::min_buffer_usage() and
	 * \sa reader_factory::multiple_buffer_usage()
	 */
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

/// A reader factory
class FZ_PUBLIC_SYMBOL reader_factory
{
public:
	explicit reader_factory(std::wstring const& name)
	    : name_(name)
	{}

	virtual ~reader_factory() noexcept = default;

	/// Clones the factory
	virtual std::unique_ptr<reader_factory> clone() const = 0;

	/** \brief Creates a reader
	 *
	 * The pool must live longer than the returned reader.
	 *
	 * Seekable readers can be opened at any position. If the reader is not
	 * seekable, pass an \c offset of 0
	 *
	 * \c size can limit the amount of data the reader can returned. Note
	 * that a size limit that exceeds the actual size will result in
	 * reader_base::get_buffer evenutually returning an error.
	 */
	virtual std::unique_ptr<reader_base> open(aio_buffer_pool & pool, uint64_t offset = 0, uint64_t size = reader_base::nosize, size_t max_buffers = 0) = 0;

	virtual bool seekable() const { return false; }

	std::wstring name() const { return name_; }

	virtual uint64_t size() const { return reader_base::nosize; }
	virtual datetime mtime() const { return datetime(); }

	/** \brief The reader requires at least this many buffers
	 *
	 * Size your buffer_pool to have a least as many buffers as the sum
	 * of min_buffer_usage() of all involved readers/writers, otherwise
	 * progress may stall due to buffer exhaustion.
	 */
	virtual size_t min_buffer_usage() const { return 1; }

	/** \brief Whether the reader can benefit from multiple buffers
	 *
	 * If false, calling \c open with \c max_buffers larger than
	 * \c min_buffer_usage() offers no benefits.
	 */
	virtual bool multiple_buffer_usage() const { return false; }

protected:
	reader_factory() = default;
	reader_factory(reader_factory const&) = default;
	reader_factory& operator=(reader_factory const&) = default;

private:
	std::wstring name_;
};

/// Holder for reader factories
class FZ_PUBLIC_SYMBOL reader_factory_holder final
{
public:
	reader_factory_holder() = default;
	reader_factory_holder(std::unique_ptr<reader_factory> && factory);
	reader_factory_holder(std::unique_ptr<reader_factory> const& factory);
	reader_factory_holder(reader_factory const& factory);

	reader_factory_holder(reader_factory_holder const& op);
	reader_factory_holder& operator=(reader_factory_holder const& op);

	reader_factory_holder(reader_factory_holder && op) noexcept;
	reader_factory_holder& operator=(reader_factory_holder && op) noexcept;
	reader_factory_holder& operator=(std::unique_ptr<reader_factory> && factory);

	reader_factory const* operator->() const { return impl_.get(); }
	reader_factory* operator->() { return impl_.get(); }
	reader_factory const& operator*() const { return *impl_; }
	reader_factory & operator*() { return *impl_; }

	explicit operator bool() const { return impl_.operator bool(); }

private:
	std::unique_ptr<reader_factory> impl_;
};

class thread_pool;

/// Base class for threaded readers
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

/// File reader
class FZ_PUBLIC_SYMBOL file_reader final : public threaded_reader
{
public:
	/** \brief Constructs file reader.
	 *
	 * The passed \c thread_pool needs to live longer than the reader.
	 */
	file_reader(std::wstring && name, aio_buffer_pool & pool, file && f, thread_pool & tpool, uint64_t offset = 0, uint64_t size = nosize, size_t max_buffers = 4) noexcept;
	file_reader(std::wstring_view name, aio_buffer_pool & pool, file && f, thread_pool & tpool, uint64_t offset = 0, uint64_t size = nosize, size_t max_buffers = 4) noexcept;

	virtual ~file_reader() noexcept;

	virtual bool seekable() const override;

private:
	virtual void do_close(scoped_lock & l) override;
	virtual bool do_seek(scoped_lock & l) override;

	virtual void on_buffer_availability() override;

	void entry();

	file file_;
	thread_pool & thread_pool_;
};

/// Does not own the data, uses just one buffer
class FZ_PUBLIC_SYMBOL view_reader final : public reader_base
{
public:
	view_reader(std::wstring && name, aio_buffer_pool & pool, std::string_view data) noexcept;

	virtual ~view_reader() noexcept;

	virtual bool seekable() const override { return true; }

	virtual std::pair<aio_result, buffer_lease> get_buffer(aio_waiter & h) override;

private:
	virtual void do_close(scoped_lock & l) override;
	virtual bool do_seek(scoped_lock & l) override;

	virtual void on_buffer_availability() override;

	std::string_view const data_;
};

}
#endif
