#include "libfilezilla/impersonation.hpp"

#if FZ_UNIX

#include "libfilezilla/buffer.hpp"

#include <optional>
#include <tuple>

#include <crypt.h>
#include <pwd.h>
#include <shadow.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

namespace fz {
namespace {
struct passwd_holder {
	passwd_holder() = default;
	passwd_holder(passwd_holder const&) = delete;
	passwd_holder(passwd_holder &&) = default;

	passwd_holder& operator=(passwd_holder const&) = delete;
	passwd_holder& operator=(passwd_holder &&) = default;

	~passwd_holder() noexcept = default;

	struct passwd* pwd_{};

	struct passwd pwd_buffer_;
	fz::buffer buf_{};
};

passwd_holder get_passwd(fz::native_string const& username)
{
	passwd_holder ret;

	size_t s = 1024;
	int res{};
	do {
		s *= 2;
		ret.buf_.get(s);
		res = getpwnam_r(username.c_str(), &ret.pwd_buffer_, reinterpret_cast<char*>(ret.buf_.get(s)), s, &ret.pwd_);
	} while (res == ERANGE);

	if (res) {
		ret.pwd_ = nullptr;
	}

	return ret;
}


struct shadow_holder {
	shadow_holder() = default;
	shadow_holder(shadow_holder const&) = delete;
	shadow_holder(shadow_holder &&) = default;

	shadow_holder& operator=(shadow_holder const&) = delete;
	shadow_holder& operator=(shadow_holder &&) = default;

	~shadow_holder() noexcept = default;

	struct spwd* shadow_{};

	struct spwd shadow_buffer_;
	fz::buffer buf_{};
};

shadow_holder get_shadow(fz::native_string const& username)
{
	shadow_holder ret;

	size_t s = 1024;
	int res{};
	do {
		s *= 2;
		ret.buf_.get(s);
		res = getspnam_r(username.c_str(), &ret.shadow_buffer_, reinterpret_cast<char*>(ret.buf_.get(s)), s, &ret.shadow_);
	} while (res == ERANGE);

	if (res) {
		ret.shadow_ = nullptr;
	}

	return ret;
}
}

class impersonation_token_impl final
{
public:
	static impersonation_token_impl* get(impersonation_token const& t) {
		return t.impl_.get();
	}

	fz::native_string name_;
	fz::native_string home_;
	uid_t uid_{};
	gid_t gid_{};
};


impersonation_token::impersonation_token() = default;
impersonation_token::~impersonation_token() noexcept = default;


impersonation_token::impersonation_token(impersonation_token&&) noexcept = default;
impersonation_token& impersonation_token::operator=(impersonation_token&&) noexcept = default;

impersonation_token::impersonation_token(fz::native_string const& username, fz::native_string const& passwd)
{
	auto pwd = get_passwd(username);
	if (pwd.pwd_) {
		auto shadow = get_shadow(username);
		if (shadow.shadow_) {
			struct crypt_data data{};
			char* encrypted = crypt_r(passwd.c_str(), shadow.shadow_->sp_pwdp, &data);
			if (encrypted && !strcmp(encrypted, shadow.shadow_->sp_pwdp)) {
				impl_ = std::make_unique<impersonation_token_impl>();
				impl_->name_ = username;
				if (pwd.pwd_->pw_dir) {
					impl_->home_ = pwd.pwd_->pw_dir;
				}
				impl_->uid_ = pwd.pwd_->pw_uid;
				impl_->gid_ = pwd.pwd_->pw_gid;
			}
		}
	}
}

impersonation_token::impersonation_token(fz::native_string const& username, impersonation_flag flag)
{
	if (flag == impersonation_flag::pwless) {
		auto pwd = get_passwd(username);
		if (pwd.pwd_) {
			impl_ = std::make_unique<impersonation_token_impl>();
			impl_->name_ = username;
			if (pwd.pwd_->pw_dir) {
				impl_->home_ = pwd.pwd_->pw_dir;
			}
			impl_->uid_ = pwd.pwd_->pw_uid;
			impl_->gid_ = pwd.pwd_->pw_gid;
		}
	}
}

fz::native_string impersonation_token::username() const
{
	return impl_ ? impl_->name_ : fz::native_string();
}

std::size_t impersonation_token::hash() const noexcept
{
	return impl_ ? std::hash<fz::native_string>{}(impl_->name_) : std::hash<fz::native_string>{}(fz::native_string());
}

// Note: Setuid binaries
bool set_process_impersonation(impersonation_token const& token)
{
	auto impl = impersonation_token_impl::get(token);
	if (!impl) {
		return false;
	}

	if (setgid(impl->gid_) != 0) {
		return false;
	}
	if (setuid(impl->uid_) != 0) {
		return false;
	}

	return true;
}

bool impersonation_token::operator==(impersonation_token const& op) const
{
	if (!impl_) {
		return !op.impl_;
	}
	if (!op.impl_) {
		return false;
	}

	return std::tie(impl_->name_, impl_->uid_, impl_->gid_, impl_->home_) == std::tie(op.impl_->name_, op.impl_->uid_, op.impl_->gid_, op.impl_->home_);
}

bool impersonation_token::operator<(impersonation_token const& op) const
{
	if (!impl_) {
		return bool(op.impl_);
	}
	if (!op.impl_) {
		return false;
	}

	return std::tie(impl_->name_, impl_->uid_, impl_->gid_, impl_->home_) < std::tie(op.impl_->name_, op.impl_->uid_, op.impl_->gid_, op.impl_->home_);
}

fz::native_string impersonation_token::home() const
{
	return impl_ ? impl_->home_ : fz::native_string();
}

}


#elif FZ_WINDOWS

#include "windows/dll.hpp"

#include "libfilezilla/glue/windows.hpp"
#include "windows/security_descriptor_builder.hpp"

#include <shlobj.h>

#include <tuple>

namespace fz {
class impersonation_token_impl final
{
public:
	impersonation_token_impl() = default;

	static impersonation_token_impl* get(impersonation_token const& t) {
		return t.impl_.get();
	}

	~impersonation_token_impl() {
		if (h_ != INVALID_HANDLE_VALUE) {
			CloseHandle(h_);
		}
	}

	static HANDLE get_handle(impersonation_token const& t) {
		return t.impl_ ? t.impl_->h_ : INVALID_HANDLE_VALUE;
	}

	impersonation_token_impl(impersonation_token_impl const&) = delete;
	impersonation_token_impl& operator=(impersonation_token_impl const&) = delete;

	fz::native_string name_;
	std::string sid_; // SID as string
	HANDLE h_{INVALID_HANDLE_VALUE};
};

impersonation_token::impersonation_token() = default;
impersonation_token::~impersonation_token() noexcept = default;

impersonation_token::impersonation_token(impersonation_token&&) noexcept = default;
impersonation_token& impersonation_token::operator=(impersonation_token&&) noexcept = default;


impersonation_token::impersonation_token(fz::native_string const& username, fz::native_string const& passwd)
{
	if (username.find_first_of(L"\"/\\[]:;|=,+*?<>") != fz::native_string::npos) {
		return;
	}

	HANDLE token{INVALID_HANDLE_VALUE};
	DWORD res = LogonUserW(username.c_str(), nullptr, passwd.c_str(), LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &token);
	if (!res) {
		return;
	}

	HANDLE primary{INVALID_HANDLE_VALUE};
	res = DuplicateTokenEx(token, 0, nullptr, SecurityImpersonation, TokenPrimary, &primary);
	if (res != 0) {
		std::string sid = GetSidFromToken(primary);
		if (!sid.empty()) {
			impl_ = std::make_unique<impersonation_token_impl>();
			impl_->name_ = username;
			impl_->sid_ = std::move(sid);
			impl_->h_ = primary;
		}
		else {
			CloseHandle(primary);
		}
	}
	CloseHandle(token);
}

fz::native_string impersonation_token::username() const
{
	return impl_ ? impl_->name_ : fz::native_string();
}

std::size_t impersonation_token::hash() const noexcept
{
	return impl_ ? std::hash<fz::native_string>{}(impl_->name_) : std::hash<fz::native_string>{}(fz::native_string());
}

bool impersonation_token::operator==(impersonation_token const& op) const
{
	if (!impl_) {
		return !op.impl_;
	}
	if (!op.impl_) {
		return false;
	}

	return std::tie(impl_->name_, impl_->sid_) == std::tie(op.impl_->name_, op.impl_->sid_);
}

bool impersonation_token::operator<(impersonation_token const& op) const
{
	if (!impl_) {
		return bool(op.impl_);
	}
	if (!op.impl_) {
		return false;
	}

	return std::tie(impl_->name_, impl_->sid_) < std::tie(op.impl_->name_, op.impl_->sid_);
}

namespace {
extern "C" {
typedef HRESULT (*getknownfolderpath_t)(REFKNOWNFOLDERID, DWORD, HANDLE, wchar_t**);
typedef HRESULT (*getknownfolderpath_t)(REFKNOWNFOLDERID, DWORD, HANDLE, wchar_t**);
typedef void (*cotaskmemfree_t)(void*);
}
}

fz::native_string impersonation_token::home() const
{
	fz::native_string ret;

	if (impl_) {
		wchar_t* out{};
		// Manually define it instead of using FOLDERID_Profile as it would prevent building a DLL.
		static GUID const profile = { 0x5E6C858F, 0x0E22, 0x4760, {0x9A, 0xFE, 0xEA, 0x33, 0x17, 0xB6, 0x71, 0x73} };

		static dll const shell32(L"shell32.dll");
		static dll const ole32(L"ole32.dll");
		static getknownfolderpath_t const getknownfolderpath = shell32 ? reinterpret_cast<getknownfolderpath_t>(GetProcAddress(shell32.h_, "SHGetKnownFolderPath")) : nullptr;
		static cotaskmemfree_t const cotaskmemfree = ole32 ? reinterpret_cast<cotaskmemfree_t>(GetProcAddress(ole32.h_, "CoTaskMemFree")) : nullptr;
		
		if (getknownfolderpath && cotaskmemfree && getknownfolderpath(profile, 0, impl_->h_, &out) == S_OK) {
			ret = out;
			cotaskmemfree(out);
		}
	}
	return ret;
}

HANDLE get_handle(impersonation_token const& t) {
	return impersonation_token_impl::get_handle(t);
}

}

#elif 0
namespace fz {
struct impersonation_token_impl{};
impersonation_token::impersonation_token() {}
impersonation_token::impersonation_token(fz::native_string const&, fz::native_string const&) {}
impersonation_token::impersonation_token(fz::native_string const&, impersonation_flag) {}
impersonation_token::~impersonation_token() noexcept {}

bool set_process_impersonation(impersonation_token const&)
{
	return false;
}
}
#endif
