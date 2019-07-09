#include <libfilezilla/buffer.hpp>
#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/format.hpp>
#include <libfilezilla/logger.hpp>
#include <libfilezilla/tls_layer.hpp>
#include <libfilezilla/tls_info.hpp>
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

struct connection final
{
	std::unique_ptr<fz::socket> s_;
	std::unique_ptr<fz::tls_layer> tls_;
	fz::socket_layer* layer_{};

	fz::buffer send_buffer_;
	size_t expected_{};

	int eof_{};
	bool can_send_{};

	fz::datetime start_;
};

struct connection_limit_event_type;
typedef fz::simple_event<connection_limit_event_type, size_t> connection_limit_event;
}

struct client final : public fz::event_handler
{
	explicit client(fz::thread_pool & pool, fz::event_loop & loop, unsigned short port);
	virtual ~client();

	void create_conn();

	virtual void operator()(fz::event_base const& ev) override;
	void on_socket_event(fz::socket_event_source*, fz::socket_event_flag t, int error);

	void on_write(std::unordered_map<fz::socket_event_source *, connection>::iterator it);

	void on_cert(fz::tls_layer* layer, fz::tls_session_info const& info);

	void on_set_connection_limit(size_t limit);

	void on_timer(fz::timer_id const&);

	void done(std::unordered_map<fz::socket_event_source *, connection>::iterator it, char result);

	fz::thread_pool & pool_;
	fz::event_loop & loop_;

	fz::timer_id timer_;

	unsigned short const port_;

	size_t connection_limit_{};

	std::unordered_map<fz::socket_event_source *, connection> connections_;

	std::unordered_map<char, std::pair<size_t, fz::duration>> stats_;
};

client::client(fz::thread_pool & pool, fz::event_loop & loop, unsigned short port)
	: fz::event_handler(loop)
	, pool_(pool)
	, loop_(loop)
	, port_(port)
{
	timer_ = add_timer(fz::duration::from_seconds(1), false);
}

client::~client()
{
	remove_handler();
	connections_.clear();
}

void client::done(std::unordered_map<fz::socket_event_source *, connection>::iterator it, char result)
{
	auto & conn = it->second;
	auto duration = fz::datetime::now() - conn.start_;

	auto & stats = stats_[result];
	++stats.first;
	stats.second += duration;

	std::cout << result << std::flush;
	connections_.erase(it);
	send_event<connection_limit_event>(0);
}

void client::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::socket_event, fz::certificate_verification_event, connection_limit_event, fz::timer_event>(ev, this,
		&client::on_socket_event, &client::on_cert, &client::on_set_connection_limit, &client::on_timer);
}

void client::on_timer(fz::timer_id const&)
{
	if (!stats_.empty()) {
		std::cout << "\n";
		for (auto const& stat : stats_) {
			std::cout << stat.first << " " << stat.second.first << " " << stat.second.second.get_milliseconds() / stat.second.first << "\n";
		}
		std::cout << std::flush;
		stats_.clear();
	}
	if (!connections_.empty()) {
		fz::duration busy;
		auto const now = fz::datetime::now();
		for (auto const& it : connections_) {
			auto const& conn = it.second;
			busy += now - conn.start_;
		}
		std::cout << "? " << connections_.size() << " " << busy.get_milliseconds() / connections_.size() << "\n";
	}
}

void client::on_cert(fz::tls_layer * layer, fz::tls_session_info const&)
{
	layer->set_verification_result(true);
}

void client::create_conn()
{
	connection conn;
	conn.start_ = fz::datetime::now();
	conn.s_ = std::make_unique<fz::socket>(pool_, nullptr);
	conn.s_->set_flags(conn.s_->flags() | fz::socket::flag_nodelay);

	if (conn.s_->connect("localhost", port_, fz::address_type::unknown) != 0) {
		std::cout << "c" << std::flush;
		return;
	}
	conn.tls_ = std::make_unique<fz::tls_layer>(loop_, this, *conn.s_, nullptr, log);
	if (!conn.tls_->client_handshake(this)) {
		std::cout << "h" << std::flush;
		return;
	}
	conn.layer_ = conn.tls_.get();
	conn.expected_ = 1024*1024;
	conn.send_buffer_.append(fz::sprintf("%d\n", conn.expected_));
	connections_[conn.s_.get()] = std::move(conn);
}

void client::on_socket_event(fz::socket_event_source* s, fz::socket_event_flag t, int error)
{
	if (t == fz::socket_event_flag::connection_next) {
		return;
	}

	auto it = connections_.find(s->root());
	auto & conn = it->second;

	if (error) {
		done(it, 'e');
		return;
	}

	if (t == fz::socket_event_flag::read) {
		char buf[1024];
		while (true) {
			int read = conn.layer_->read(buf, 1024, error);
			if (!read) {
				if (conn.expected_) {
					done(it, '<');
					return;
				}
				if (!conn.eof_) {
					conn.eof_ = 1;
				}
				error = conn.layer_->shutdown_read();
				if (!error) {
					conn.eof_ =2;
				}
				else if (error != EAGAIN) {
					done(it, '<');
					return;
				}
				if (conn.can_send_) {
					error = conn.layer_->shutdown();
					if (!error) {
						if (conn.eof_ == 2) {
							done(it, '.');
							return;
						}
					}
					else if (error != EAGAIN) {
						done(it, 'r');
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
					done(it, 'r');
					return;
				}
				break;
			}
			else {
				if (static_cast<size_t>(read) > conn.expected_) {
					done(it, '>');
					return;
				}
				conn.expected_ -= read;
				if (!conn.expected_ && conn.can_send_) {
					error = conn.layer_->shutdown();
					if (error == EAGAIN) {
						conn.can_send_ = false;
					}
					else if (error) {
						done(it, 'w');
						return;
					}
				}
			}
		}
	}
	else if (t == fz::socket_event_flag::write) {
		on_write(it);
	}
}

void client::on_write(std::unordered_map<fz::socket_event_source *, connection>::iterator it)
{
	auto & conn = it->second;

	conn.can_send_ = true;
	while (!conn.send_buffer_.empty()) {
		int error{};
		int written = conn.layer_->write(conn.send_buffer_.get(), conn.send_buffer_.size(), error);
		if (written < 0) {
			if (error != EAGAIN) {
				done(it, 'w');
				return;
			}
			else {
				conn.can_send_ = false;
			}
			return;
		}
		else {
			conn.send_buffer_.consume(written);
		}
	}

	if (conn.eof_ || !conn.expected_) {
		int error = conn.layer_->shutdown();
		if (!error) {
			if (conn.eof_ == 2) {
				done(it, '.');
			}
		}
		else if (error != EAGAIN) {
			done(it, 'w');
		}
		else {
			conn.can_send_ = false;
		}
	}
}

void client::on_set_connection_limit(size_t limit)
{
	if (limit) {
		connection_limit_ = limit;
	}

	if (connection_limit_ > connections_.size()) {
		size_t missing = connection_limit_ - connections_.size();
		while (missing--) {
			create_conn();
		}
	}
}

int main(int argc, char const ** const argv)
{
	if (argc < 3) {
		std::cerr << "Need to pass port and connection limit" << std::endl;
		exit(1);
	}

	int port = fz::to_integral<unsigned short>(std::string_view(argv[1]));

	fz::thread_pool pool;

	fz::event_loop loop(fz::event_loop::threadless);

	client s(pool, loop, port);

	s.send_event<connection_limit_event>(fz::to_integral<size_t>(std::string_view(argv[2])));

	loop.run();

	return 0;
}
