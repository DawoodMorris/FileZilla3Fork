#include <libfilezilla/event_handler.hpp>
#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/buffer.hpp>
#include <libfilezilla/logger.hpp>
#include <libfilezilla/process.hpp>
#include <libfilezilla/socket.hpp>
#include <libfilezilla/thread_pool.hpp>
#include <libfilezilla/tls_layer.hpp>
#include <libfilezilla/tls_info.hpp>
#include <libfilezilla/util.hpp>

#include <map>
#include <iostream>
#include <string>

namespace {
// Helper function to extract a directory from argv[0] so that the
// demos can be run independent of the current working directory.
fz::native_string get_program_dir(int argc, char ** argv)
{
	std::string_view path;
	if (argc > 0) {
		path = argv[0];
#ifdef FZ_WINDOWS
		auto delim = path.find_last_of("/\\");
#else
		auto delim = path.find_last_of("/");
#endif
		if (delim == std::string::npos) {
			path = std::string_view();
		}
		else {
			path = path.substr(0, delim + 1);
		}
	}

	return fz::to_native(path);
}

#ifdef FZ_WINDOWS
auto suffix = fzT(".exe");
#else
auto suffix = fzT("");
#endif

struct credentials {
	std::string host;
	unsigned short port{};
	std::string user;
	std::string pass;
	bool implicit_tls{};
};

struct logger : public fz::logger_interface
{
	virtual void do_log(fz::logmsg::type, std::wstring &&) {
	}
};

}


class client final : public fz::event_handler
{
public:
	client(fz::event_loop & loop, credentials const& creds)
		: fz::event_handler(loop)
		, creds_(creds)
	{
		s_ = std::make_unique<fz::socket>(pool_, this);
		si_ = s_.get();
		if (creds.implicit_tls) {
			tls_ = std::make_unique<fz::tls_layer>(loop, this, *s_, nullptr, logger_);
			si_ = tls_.get();
		}

		if (si_->connect(creds_.host, creds_.port)) {
			std::cerr << "Connect failed\n";
			exit(1);
		}

		if (tls_ && !tls_->client_handshake(this)) {
			std::cerr << "Handshake failed\n";
			exit(1);
		}
	}

	~client()
	{
		remove_handler();
	}

	virtual void operator()(fz::event_base const& ev) override
	{
		fz::dispatch<fz::socket_event, fz::certificate_verification_event>(ev, this, &client::on_socket_event, &client::on_cert);
	}

	void on_cert(fz::tls_layer* l, fz::tls_session_info const&)
	{
		l->set_verification_result(true);
	}

	void on_socket_event(fz::socket_event_source * s, fz::socket_event_flag flag, int error)
	{
		if (error) {
			std::cerr << "Socket error on event " << (int)flag << "\n";
			exit(1);
		}
		if (flag == fz::socket_event_flag::read) {
			int r = si_->read(inbuffer_.get(4096), 4096, error);
			if (!r) {
				std::cerr << "EOF\n";
				exit(1);
			}
			if (r < 0 && error != EAGAIN) {
				std::cerr << "Error reading: " << error << "\n";
				exit(1);
			}


			if (r > 0) {
				send_event<fz::socket_event>(s, flag, 0);
			}
		}
		else if (flag == fz::socket_event_flag::write) {
			while (outbuffer_.size() < 4096) {
				outbuffer_.append("NOOP\r\n");
			}

			int w = si_->write(outbuffer_.get(), outbuffer_.size(), error);
			if (!w) {
				std::cerr << "EOF\n";
				exit(1);
			}
			if (w < 0 && error != EAGAIN) {
				std::cerr << "Error writing: " << error << "\n";
				exit(1);
			}

			if (w > 0) {
				outbuffer_.consume(w);
				send_event<fz::socket_event>(s, flag, 0);
			}
		}
	}

	fz::buffer inbuffer_;
	fz::buffer outbuffer_;
	credentials creds_;

	fz::thread_pool pool_;
	logger logger_;
	std::unique_ptr<fz::socket> s_;
	std::unique_ptr<fz::tls_layer> tls_;
	fz::socket_interface* si_{};
};

class runner final : public fz::event_handler
{
public:
	runner(fz::event_loop& l, fz::native_string const& self, std::vector<fz::native_string> const& args)
		: fz::event_handler(l)
		, self_(self)
		, args_(args)
	{
		t_ = add_timer(fz::duration::from_milliseconds(10), false);
	}

	~runner()
	{
		remove_handler();
	}

	virtual void operator()(fz::event_base const& ev) override
	{
		fz::dispatch<fz::timer_event>(ev, this, &runner::on_timer);
	}

	void on_timer(fz::timer_id const& id)
	{
		if (id == t_) {
			if (workers_.size() < 100) {
				std::cerr << ".";
				auto p = std::make_unique<fz::process>();

				if (!p->spawn(self_, args_, false)) {
					std::cerr << "Spawn failed\n";
				}
				fz::timer_id t = add_timer(fz::duration::from_milliseconds(fz::random_number(1, 5000)), true);
				workers_[t] = std::move(p);
			}
		}
		else {
			workers_.erase(id);
		}
	}

	fz::native_string self_;
	std::vector<fz::native_string> args_;

	fz::timer_id t_;
	std::map<fz::timer_id, std::unique_ptr<fz::process>> workers_;
};

int main(int argc, char *argv[])
{
	if (argc < 6) {
		std::cerr << "Wrong args\n";
		return 1;
	}

	fz::event_loop loop(fz::event_loop::threadless);

	if (argc > 6) {
		credentials creds;
		creds.host = argv[1];
		creds.port = fz::to_integral<unsigned short>(argv[2]);
		creds.user = argv[3];
		creds.pass = argv[4];
		creds.implicit_tls = argv[5][0] == '1';

		client c(loop, creds);
		loop.run();
	}
	else {
		std::vector<fz::native_string> args;
		for (size_t i = 1; i <= 5; ++i) {
			args.push_back(fz::to_native(argv[i]));
		}
		args.push_back(fz::to_native("1"));
		runner r(loop, get_program_dir(argc, argv) + fzT("ftpstresstest") + suffix, args);

		loop.run();
	}

	return 0;
}
