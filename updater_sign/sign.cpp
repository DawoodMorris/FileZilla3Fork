#include "key.hpp"

#include <libfilezilla/buffer.hpp>
#include <libfilezilla/encode.hpp>
#include <libfilezilla/file.hpp>
#include <libfilezilla/hash.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/signature.hpp>

#include <functional>
#include <iostream>
#include <string_view>

#include <errno.h>
#include <unistd.h>

auto const priv = fz::private_signing_key::from_base64(key);

std::vector<uint8_t> process(std::function<bool(fz::buffer & buf)> reader, bool hash, std::string const& tag)
{
	fz::buffer in;
	fz::hash_accumulator acc(fz::hash_algorithm::sha512);

	while (true) {

		if (!reader(in)) {
			break;
		}

		if (hash) {
			acc.update(in.get(), in.size());
			in.clear();
		}
	}

	if (hash) {
		auto raw = acc.digest();
		if (!tag.empty()) {
			raw.push_back(0);
			raw.insert(raw.end(), tag.cbegin(), tag.cend());
		}
		return fz::sign(raw, priv, false);
	}
	else {
		if (!in.size()) {
			std::cerr << "Nothing to sign\n";
			exit(1);
		}
		if (!tag.empty()) {
			*in.get(1) = 0;
			in.add(1);
			in.append(tag);
		}
		return fz::sign(in.get(), in.size(), priv, false);
	}
}

int main(int argc, char const* argv[])
{
	bool hash{};
	bool base64{};
	std::string tag;

	std::vector<std::string_view> files;

	for (int i = 1; i < argc; ++i) {
		if (!argv[i] || !(*argv[i])) {
			continue;
		}

		std::string_view arg(argv[i]);
		if (arg == "--pub") {
			std::cout << priv.pubkey().to_base64() << "\n";
			return 0;
		}
		else if (arg == "--genpriv") {
			std::cout << fz::private_signing_key::generate().to_base64() << "\n";
			return 0;
		}
		else if (arg == "--hash") {
			hash = true;
		}
		else if (arg == "--base64") {
			base64 = true;
		}
		else if (arg == "--tag") {
			if (++i >= argc) {
				std::cerr << "Need tag value\n";
				return 1;
			}
			tag = argv[i];
			if (tag.empty()) {
				std::cerr << "Need non-empty tag value\n";
				return 1;
			}
		}
		else if (arg.substr(0, 2) == "--") {
			if (arg.size() == 2) {
				while (++i < argc) {
					if (!argv[i] || !(*argv[i])) {
						continue;
					}
					files.emplace_back(argv[i]);
				}
				break;
			}
			else {
				std::cout << "Unknown option: " << arg << "\n";
				exit(1);
			}
		}
		else {
			files.emplace_back(arg);
		}
	}

	if (!priv) {
		std::cerr << "No key. Run --genpriv, put result in key.hpp and rebuilt\n";
		exit(1);
	}

	if (files.empty()) {
		auto reader = [](fz::buffer & in) {
			while (true) {
				ssize_t r = read(STDIN_FILENO, in.get(4096), 4096);

				if (!r) {
					return false;
				}
				else if (r > 0) {
					in.add(r);
					return true;
				}
				else {
					if (errno != EAGAIN && errno != EINTR) {
						std::cerr << "Reading from stdin failed\n";
						exit(1);
					}
				}
			}
			return false;
		};

		auto sig = process(reader, hash, tag);
			if (base64) {
				std::cout << fz::base64_encode(sig, fz::base64_type::standard, false) << std::endl;
			}
			else {
				std::cout << fz::hex_encode<std::string>(sig) << std::endl;
			}
	}
	else {
		for (auto const& name : files) {
			if (fz::local_filesys::get_file_type(std::string(name), true) == fz::local_filesys::dir) {
				std::cerr << "Ignoring directory '" << name << "'\n";
				continue;
			}
			fz::file file(std::string(name), fz::file::reading);
			if (!file.opened()) {
				std::cerr << "Could not open " << name << "\n";
				exit(1);
			}
			auto reader = [&](fz::buffer & in) {
				int r = file.read(in.get(4096), 4096);
				if (!r) {
					return false;
				}
				else if (r > 0) {
					in.add(r);
					return true;
				}
				else {
					std::cerr << "Could not read from '" << name << "'\n";
					exit(1);
				}
			};

			auto sig = process(reader, hash, tag);
			if (base64) {
				std::cout << fz::base64_encode(sig, fz::base64_type::standard, false);
			}
			else {
				std::cout << fz::hex_encode<std::string>(sig);
			}
			std::cout << " " << (hash ? "" : "*") << name << std::endl;
		}
	}

	return 0;
}
