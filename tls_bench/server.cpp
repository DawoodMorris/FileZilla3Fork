#include <libfilezilla/buffer.hpp>
#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/logger.hpp>
#include <libfilezilla/tls_layer.hpp>
#include <libfilezilla/thread_pool.hpp>
#include <libfilezilla/util.hpp>

#include <iostream>
#include <unordered_map>

namespace {
struct logger final : public fz::logger_interface
{
	virtual void do_log(fz::logmsg::type, std::wstring &&) override {
	};
};

logger log;

auto const& get_key_and_cert()
{
	static auto key_and_cert = fz::tls_layer::generate_selfsigned_certificate(fz::native_string(), "CN=tls_bench", {});
	return key_and_cert;
}

std::vector<uint8_t> send_buffer;
}

struct connection final
{
	std::unique_ptr<fz::socket> s_;
	std::unique_ptr<fz::tls_layer> tls_;
	fz::socket_layer* layer_{};

	fz::buffer recv_buffer_;

	int eof_{};
	bool can_send_{};
	size_t to_send_{};
};

struct accept_event_type;
typedef fz::simple_event<accept_event_type> accept_event;

struct worker final : public fz::event_handler
{
	explicit worker(fz::event_loop & loop, fz::listen_socket& ls);
	virtual ~worker();

	virtual void operator()(fz::event_base const& ev) override;
	void on_socket_event(fz::socket_event_source*, fz::socket_event_flag t, int error);
	void on_accept();

	void on_write(std::unordered_map<fz::socket_event_source *, connection>::iterator it);

	fz::event_loop & loop_;

	fz::listen_socket & ls_;

	std::unordered_map<fz::socket_event_source *, connection> connections_;
};

worker::worker(fz::event_loop & loop, fz::listen_socket & ls)
	: fz::event_handler(loop)
	, loop_(loop)
	, ls_(ls)
{
}

worker::~worker()
{
	remove_handler();
	connections_.clear();
}

void worker::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::socket_event, accept_event>(ev, this,
		&worker::on_socket_event,
		&worker::on_accept);
}

void worker::on_socket_event(fz::socket_event_source* s, fz::socket_event_flag t, int error)
{
	auto it = connections_.find(s->root());
	auto & conn = it->second;

	if (error) {
		std::cout << "e" << std::flush;
		connections_.erase(it);
		return;
	}

	if (t == fz::socket_event_flag::read) {
		while (true) {
			int error{};
			int read = conn.layer_->read(conn.recv_buffer_.get(1024), 1024, error);
			if (!read) {
				if (!conn.eof_) {
					conn.eof_ = 1;
				}
				int error = conn.layer_->shutdown_read();
				if (error != EAGAIN) {
					if (!error) {
						conn.eof_ = 2;
					}
					else {
						std::cout << "r" << std::flush;
						connections_.erase(it);
						return;
					}
				}
				if (!conn.to_send_ && conn.can_send_) {
					error = conn.layer_->shutdown();
					if (!error) {
						if (conn.eof_ == 2) {
//							std::cout << "." << std::flush;
							connections_.erase(it);
							return;
						}
					}
					else if (error != EAGAIN) {
						std::cout << "w" << std::flush;
						connections_.erase(it);
						return;
					}
					else {
						conn.can_send_ = false;
					}
				}

				break;
			}
			else if (read < 0) {
				if (error != EAGAIN) {
					std::cout << "r" << std::flush;
					connections_.erase(it);
					return;
				}
				break;
			}
			else {
				conn.recv_buffer_.add(read);
				size_t i = conn.recv_buffer_.size() - read;
				while (i < conn.recv_buffer_.size()) {
					if (conn.recv_buffer_[i] != '\n') {
						++i;
						continue;
					}
					std::string_view v(reinterpret_cast<char const*>(conn.recv_buffer_.get()), i);
					conn.to_send_ += fz::to_integral<size_t>(v);
					if (conn.can_send_) {
						conn.can_send_ = false;
						send_event<fz::socket_event>(s, fz::socket_event_flag::write, 0);
					}
					conn.recv_buffer_.consume(i + 1);
					i = 0;
				}
			}
		}
	}
	else if (t == fz::socket_event_flag::write) {
		on_write(it);
	}
}

void worker::on_accept()
{
	int err{};

	connection conn;
	conn.s_ = ls_.accept(err);
	if (!conn.s_) {
		std::cout << "c" << std::flush;
		return;
	}
	conn.s_->set_flags(conn.s_->flags() | fz::socket::flag_nodelay);

	conn.tls_ = std::make_unique<fz::tls_layer>(loop_, this, *conn.s_, nullptr, log);
	conn.tls_->set_certificate(get_key_and_cert().first, get_key_and_cert().second, fz::native_string());
	if (!conn.tls_->server_handshake()) {
		std::cout << "h" << std::flush;
		return;
	}
	conn.layer_ = conn.tls_.get();
	connections_[conn.s_.get()] = std::move(conn);
}

void worker::on_write(std::unordered_map<fz::socket_event_source *, connection>::iterator it)
{
	auto & conn = it->second;

	conn.can_send_ = true;
	while (conn.to_send_) {
		size_t to_send = std::min(conn.to_send_, send_buffer.size());
		int error{};
		int written = conn.layer_->write(send_buffer.data(), to_send, error);
		if (written < 0) {
			if (error != EAGAIN) {
				std::cout << "w" << std::flush;
				connections_.erase(it);
			}
			else {
				conn.can_send_ = false;
			}
				return;
		}
		else {
			conn.to_send_ -= written;
		}
	}

	if (conn.eof_) {
		int error = conn.layer_->shutdown();
		if (!error) {
			if (conn.eof_ == 2) {
//				std::cout << "." << std::flush;
				connections_.erase(it);
			}
		}
		else if (error != EAGAIN) {
			std::cout << "w" << std::flush;
			connections_.erase(it);
		}
		else {
			conn.can_send_ = false;
		}
	}
}


struct server final : public fz::event_handler
{
	explicit server(fz::thread_pool & pool, fz::event_loop & loop, unsigned short port, size_t workers);
	virtual ~server();

	virtual void operator()(fz::event_base const& ev) override;
	void on_socket_event(fz::socket_event_source*, fz::socket_event_flag t, int error);

	fz::event_loop & loop_;

	std::unique_ptr<fz::listen_socket> ls_;

	std::vector<std::pair<std::unique_ptr<fz::event_loop>, std::unique_ptr<worker>>> workers_;
	size_t worker_{};
};


server::server(fz::thread_pool & pool, fz::event_loop & loop, unsigned short port, size_t workers)
	: fz::event_handler(loop)
	, loop_(loop)
{
	ls_ = std::make_unique<fz::listen_socket>(pool, this);
	if (ls_->listen(fz::address_type::unknown, static_cast<int>(port)) != 0) {
		std::cerr << "Listen failed\n";
		exit(1);
	}
	std::cerr << "Now listening on port " << port << "\n";

	workers_.reserve(workers);
	while (workers--) {
		auto wl = std::make_unique<fz::event_loop>(pool);
		workers_.push_back(std::make_pair(std::move(wl), std::make_unique<worker>(*wl, *ls_)));
	}
}

server::~server()
{
	remove_handler();
	ls_.reset();
}

void server::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::socket_event>(ev, this,
		&server::on_socket_event);
}

void server::on_socket_event(fz::socket_event_source* s, fz::socket_event_flag, int error)
{
	if (error) {
		return;
	}
	if (s != ls_.get()) {
		return;
	}

	auto & w = workers_[worker_].second;
	w->send_event<accept_event>();
	if (++worker_ == workers_.size()) {
		worker_ = 0;
	}
}

int main(int argc, char const ** const argv)
{
	if (argc < 3) {
		std::cerr << "Need to pass port and workers\n";
		exit(1);
	}

	send_buffer = fz::random_bytes(1024 * 1024);

	unsigned short const port = fz::to_integral<unsigned short>(std::string_view(argv[1]));
	size_t const workers = fz::to_integral<size_t>(std::string_view(argv[2]));
	if (!workers) {
		std::cerr << "No workers.\n";
		exit(1);
	}

	fz::thread_pool pool;

	fz::event_loop loop(fz::event_loop::threadless);

	server s(pool, loop, port, workers);

	loop.run();

	return 0;
}
