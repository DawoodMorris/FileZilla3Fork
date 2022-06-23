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

	/// Must be finalized already
	virtual bool set_mtime(datetime const&) { return false; }

	void close();

	using progress_cb_t = std::function<void(writer_base const*, uint64_t written)>;

protected:
	// Progress callback is only for accounting progress. Never call into the writer from the callback.
	// Idiomatic usage of the the progress callback:
	//   Update some atomic variables and optionally send an event.
	writer_base(std::wstring && name, aio_buffer_pool & pool, progress_cb_t && progress_cb, size_t max_buffers) noexcept
	    : buffer_pool_(pool)
	    , name_(name)
	    , progress_cb_(std::move(progress_cb))
	    , max_buffers_(max_buffers ? max_buffers : 1)
	{}

	writer_base(std::wstring_view name, aio_buffer_pool & pool, progress_cb_t && progress_cb, size_t max_buffers) noexcept
	    : buffer_pool_(pool)
	    , name_(name)
	    , progress_cb_(std::move(progress_cb))
	    , max_buffers_(max_buffers ? max_buffers : 1)
	{}

	virtual void do_close(scoped_lock &) {}

	mutex mtx_;
	aio_buffer_pool & buffer_pool_;

	std::wstring const name_;

	progress_cb_t progress_cb_;

	size_t const max_buffers_{};
	std::list<buffer_lease> buffers_;

	bool error_{};
	uint8_t finalizing_{};
};

class FZ_PUBLIC_SYMBOL writer_factory
{
public:
	explicit writer_factory(std::wstring const& name)
	    : name_(name)
	{}

	virtual ~writer_factory() noexcept = default;

	virtual std::unique_ptr<writer_factory> clone() const = 0;

	virtual std::unique_ptr<writer_base> open(aio_buffer_pool & pool, uint64_t offset = 0, writer_base::progress_cb_t progress_cb = nullptr, size_t max_buffers = 0) = 0;

	std::wstring name() const { return name_; }

	virtual uint64_t size() const { return writer_base::nosize; }
	virtual datetime mtime() const { return datetime(); }

	/// The writer requires at least this many buffers
	virtual size_t min_buffer_usage() const { return 1; }

	/// Whether the writer can benefit from multiple buffers
	virtual bool multiple_buffer_usage() const { return false; }

	virtual size_t preferred_buffer_count() const { return 1; }

	/** \brief Sets the mtime of the target.
	 *
	 * If there are still writers open for the entity represented by the
	 * factory, the mtime might change again as the writers are closed.
	 */
	virtual bool set_mtime(datetime const&) { return false; }
protected:
	writer_factory() = default;
	writer_factory(writer_factory const&) = default;
	writer_factory& operator=(writer_factory const&) = default;

private:
	std::wstring name_;
};

class FZ_PUBLIC_SYMBOL writer_factory_holder final
{
public:
	writer_factory_holder() = default;
	writer_factory_holder(std::unique_ptr<writer_factory> && factory);
	writer_factory_holder(std::unique_ptr<writer_factory> const& factory);
	writer_factory_holder(writer_factory const& factory);

	writer_factory_holder(writer_factory_holder const& op);
	writer_factory_holder& operator=(writer_factory_holder const& op);

	writer_factory_holder(writer_factory_holder && op) noexcept;
	writer_factory_holder& operator=(writer_factory_holder && op) noexcept;
	writer_factory_holder& operator=(std::unique_ptr<writer_factory> && factory);

	writer_factory const* operator->() const { return impl_.get(); }
	writer_factory* operator->() { return impl_.get(); }
	writer_factory const& operator*() const { return *impl_; }
	writer_factory & operator*() { return *impl_; }

	explicit operator bool() const { return impl_.operator bool(); }

private:
	std::unique_ptr<writer_factory> impl_;
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
	virtual void do_close(scoped_lock & l) override;

	virtual aio_result continue_finalize(aio_waiter &, scoped_lock &) {
		return aio_result::ok;
	}

	condition cond_;
	async_task task_;

	bool quit_{};
};

class FZ_PUBLIC_SYMBOL file_writer final : public threaded_writer
{
public:
	file_writer(std::wstring && name, aio_buffer_pool & pool, file && f, thread_pool & tpool, bool fsync = false, progress_cb_t && progress_cb = nullptr, size_t max_buffers = 4) noexcept;
	file_writer(std::wstring_view name, aio_buffer_pool & pool, file && f, thread_pool & tpool, bool fsync = false, progress_cb_t && progress_cb = nullptr, size_t max_buffers = 4) noexcept;

	virtual ~file_writer() override;

	virtual bool offsettable() const override { return true; }

	virtual aio_result preallocate(uint64_t size) override;
	virtual bool set_mtime(datetime const& t);

protected:
	virtual void do_close(scoped_lock & l) override;
	virtual aio_result continue_finalize(aio_waiter & h, scoped_lock & l) override;

private:

	void entry();

	file file_;

	bool fsync_{};
	bool preallocated_{};
};

enum class file_writer_flags : unsigned {
	fsync = 0x01,
	permissions_current_user_only = 0x02,
	permissions_current_user_and_admins_only = 0x04
};
inline bool operator&(file_writer_flags lhs, file_writer_flags rhs) {
	return (static_cast<std::underlying_type_t<file_writer_flags>>(lhs) & static_cast<std::underlying_type_t<file_writer_flags>>(rhs)) != 0;
}
inline file_writer_flags operator|(file_writer_flags lhs, file_writer_flags rhs) {
	return static_cast<file_writer_flags>(static_cast<std::underlying_type_t<file_writer_flags>>(lhs) | static_cast<std::underlying_type_t<file_writer_flags>>(rhs));
}

class FZ_PUBLIC_SYMBOL file_writer_factory final : public writer_factory
{
public:
	file_writer_factory(std::wstring const& file, thread_pool & tpool, file_writer_flags = {});

	virtual std::unique_ptr<writer_base> open(aio_buffer_pool & pool, uint64_t offset, writer_base::progress_cb_t progress_cb = nullptr, size_t max_buffers = 0) override;
	virtual std::unique_ptr<writer_factory> clone() const override;

	virtual uint64_t size() const override;
	virtual	datetime mtime() const override;

	virtual bool set_mtime(datetime const& t) override;

	virtual bool multiple_buffer_usage() const override { return true; }

	virtual size_t preferred_buffer_count() const override { return 4; }

private:
	thread_pool & thread_pool_;
	file_writer_flags flags_;
};

}

#endif
