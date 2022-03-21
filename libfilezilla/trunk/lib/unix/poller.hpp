#ifndef LIBFILEZILLA_POLLER_HEADER
#define LIBFILEZILLA_POLLER_HEADER

#include "../libfilezilla/libfilezilla.hpp"

#if !FZ_WINDOWS

#include <poll.h>

namespace fz {
class scoped_lock;

class poller final
{
public:
	poller() = default;

	poller(poller const&) = delete;
	poller& operator=(poller const&) = delete;

	~poller();

	// zero on success, else errno
	int init();

	// Must call locked
	bool wait(scoped_lock & l)
	{
		struct pollfd fds[1]{};
		return wait(fds, 0, l);
	}

	// fds must be large enough to hold n+1 entries, but fds[n] must not be filled by caller
	bool wait(struct pollfd *fds, nfds_t n, scoped_lock & l);

	void interrupt();

#if defined(HAVE_EVENTFD)
	int event_fd_{-1};
#else
	// A pipe is used to unblock poll
	int pipe_[2]{-1, -1};
#endif
};
}

#else

#error poller.hpp is not for Windows

#endif

#endif
