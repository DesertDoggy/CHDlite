// license:BSD-3-Clause
// CHDlite OSD file implementation
// Provides implementations for osd_file and related functions from osdfile.h

#include "osdfile.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <direct.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#endif


//============================================================
//  errno_to_error_condition
//============================================================

static std::error_condition errno_to_error_condition()
{
	return std::error_condition(errno, std::generic_category());
}


//============================================================
//  POSIX / Win32 file implementation
//============================================================

#if defined(_WIN32)

class win32_osd_file : public osd_file
{
public:
	win32_osd_file(HANDLE handle) : m_handle(handle) { }

	~win32_osd_file() override
	{
		if (m_handle != INVALID_HANDLE_VALUE)
			CloseHandle(m_handle);
	}

	std::error_condition read(void *buffer, std::uint64_t offset, std::uint32_t length, std::uint32_t &actual) noexcept override
	{
		OVERLAPPED ov = {};
		ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
		ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
		DWORD bytes_read = 0;
		if (!ReadFile(m_handle, buffer, length, &bytes_read, &ov))
		{
			DWORD err = GetLastError();
			if (err != ERROR_HANDLE_EOF)
				return std::error_condition(err, std::system_category());
		}
		actual = bytes_read;
		return std::error_condition();
	}

	std::error_condition write(void const *buffer, std::uint64_t offset, std::uint32_t length, std::uint32_t &actual) noexcept override
	{
		OVERLAPPED ov = {};
		ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
		ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
		DWORD bytes_written = 0;
		if (!WriteFile(m_handle, buffer, length, &bytes_written, &ov))
			return std::error_condition(GetLastError(), std::system_category());
		actual = bytes_written;
		return std::error_condition();
	}

	std::error_condition truncate(std::uint64_t offset) noexcept override
	{
		LARGE_INTEGER li;
		li.QuadPart = static_cast<LONGLONG>(offset);
		if (!SetFilePointerEx(m_handle, li, nullptr, FILE_BEGIN))
			return std::error_condition(GetLastError(), std::system_category());
		if (!SetEndOfFile(m_handle))
			return std::error_condition(GetLastError(), std::system_category());
		return std::error_condition();
	}

	std::error_condition flush() noexcept override
	{
		if (!FlushFileBuffers(m_handle))
			return std::error_condition(GetLastError(), std::system_category());
		return std::error_condition();
	}

private:
	HANDLE m_handle;
};

#else // POSIX

class posix_osd_file : public osd_file
{
public:
	posix_osd_file(int fd) : m_fd(fd) { }

	~posix_osd_file() override
	{
		if (m_fd >= 0)
			::close(m_fd);
	}

	std::error_condition read(void *buffer, std::uint64_t offset, std::uint32_t length, std::uint32_t &actual) noexcept override
	{
		ssize_t result = ::pread(m_fd, buffer, length, static_cast<off_t>(offset));
		if (result < 0)
			return errno_to_error_condition();
		actual = static_cast<std::uint32_t>(result);
		return std::error_condition();
	}

	std::error_condition write(void const *buffer, std::uint64_t offset, std::uint32_t length, std::uint32_t &actual) noexcept override
	{
		ssize_t result = ::pwrite(m_fd, buffer, length, static_cast<off_t>(offset));
		if (result < 0)
			return errno_to_error_condition();
		actual = static_cast<std::uint32_t>(result);
		return std::error_condition();
	}

	std::error_condition truncate(std::uint64_t offset) noexcept override
	{
		if (::ftruncate(m_fd, static_cast<off_t>(offset)) < 0)
			return errno_to_error_condition();
		return std::error_condition();
	}

	std::error_condition flush() noexcept override
	{
		if (::fsync(m_fd) < 0)
			return errno_to_error_condition();
		return std::error_condition();
	}

private:
	int m_fd;
};

#endif


//============================================================
//  Create directories recursively
//============================================================

static void create_path_recursive(std::string const &path)
{
	// find last separator
	auto sep = path.find_last_of(PATH_SEPARATOR[0]);
	if (sep == std::string::npos || sep == 0)
		return;

	std::string parent = path.substr(0, sep);

#if defined(_WIN32)
	DWORD attr = GetFileAttributesA(parent.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES)
	{
		create_path_recursive(parent);
		CreateDirectoryA(parent.c_str(), nullptr);
	}
#else
	struct stat st;
	if (::stat(parent.c_str(), &st) < 0)
	{
		create_path_recursive(parent);
		::mkdir(parent.c_str(), 0777);
	}
#endif
}


//============================================================
//  osd_file::open
//============================================================

std::error_condition osd_file::open(std::string const &path, std::uint32_t openflags, ptr &file, std::uint64_t &filesize) noexcept
{
	if (openflags & OPEN_FLAG_CREATE_PATHS)
		create_path_recursive(path);

#if defined(_WIN32)
	DWORD access = 0;
	DWORD share = FILE_SHARE_READ;
	DWORD creation = OPEN_EXISTING;

	if (openflags & OPEN_FLAG_READ)
		access |= GENERIC_READ;
	if (openflags & OPEN_FLAG_WRITE)
	{
		access |= GENERIC_WRITE;
		share = 0;
	}
	if (openflags & OPEN_FLAG_CREATE)
		creation = CREATE_ALWAYS;

	HANDLE h = CreateFileA(path.c_str(), access, share, nullptr, creation, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return std::error_condition(GetLastError(), std::system_category());

	LARGE_INTEGER li;
	if (!GetFileSizeEx(h, &li))
	{
		CloseHandle(h);
		return std::error_condition(GetLastError(), std::system_category());
	}

	file = std::make_unique<win32_osd_file>(h);
	filesize = static_cast<std::uint64_t>(li.QuadPart);
	return std::error_condition();

#else // POSIX
	int flags = 0;
	if ((openflags & OPEN_FLAG_READ) && (openflags & OPEN_FLAG_WRITE))
		flags = O_RDWR;
	else if (openflags & OPEN_FLAG_WRITE)
		flags = O_WRONLY;
	else
		flags = O_RDONLY;

	if (openflags & OPEN_FLAG_CREATE)
		flags |= O_CREAT | O_TRUNC;

	int fd = ::open(path.c_str(), flags, 0666);
	if (fd < 0)
		return errno_to_error_condition();

	// Sequential read hint for kernel I/O prefetch
#if defined(__APPLE__)
	fcntl(fd, F_RDAHEAD, 1);
#elif defined(__linux__)
	posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);  // not tested yet
#endif

	struct stat st;
	if (::fstat(fd, &st) < 0)
	{
		::close(fd);
		return errno_to_error_condition();
	}

	file = std::make_unique<posix_osd_file>(fd);
	filesize = static_cast<std::uint64_t>(st.st_size);
	return std::error_condition();
#endif
}


//============================================================
//  osd_file::openpty
//============================================================

std::error_condition osd_file::openpty(ptr &file, std::string &name) noexcept
{
	return std::errc::not_supported;
}


//============================================================
//  osd_file::remove
//============================================================

std::error_condition osd_file::remove(std::string const &filename) noexcept
{
	if (std::remove(filename.c_str()) != 0)
		return errno_to_error_condition();
	return std::error_condition();
}


//============================================================
//  osd_get_physical_drive_geometry
//============================================================

bool osd_get_physical_drive_geometry(const char *filename, uint32_t *cylinders, uint32_t *heads, uint32_t *sectors, uint32_t *bps) noexcept
{
	return false; // not needed for CHDlite
}


//============================================================
//  osd_is_valid_filename_char / osd_is_valid_filepath_char
//============================================================

bool osd_is_valid_filename_char(char32_t uchar) noexcept
{
	// reject common invalid filename chars
	return uchar >= 0x20
		&& uchar != '/'
		&& uchar != '\\'
		&& uchar != ':'
		&& uchar != '*'
		&& uchar != '?'
		&& uchar != '"'
		&& uchar != '<'
		&& uchar != '>'
		&& uchar != '|';
}

bool osd_is_valid_filepath_char(char32_t uchar) noexcept
{
	// allow path separators in addition to valid filename chars
	return uchar == '/' || uchar == '\\' || uchar == ':' || osd_is_valid_filename_char(uchar);
}


//============================================================
//  DIRECTORY
//============================================================

namespace osd {

#if defined(_WIN32)

class win32_directory : public directory
{
public:
	win32_directory(std::string const &dirname)
		: m_first(true)
	{
		std::string pattern = dirname + "\\*";
		m_handle = FindFirstFileA(pattern.c_str(), &m_data);
	}

	~win32_directory() override
	{
		if (m_handle != INVALID_HANDLE_VALUE)
			FindClose(m_handle);
	}

	const entry *read() override
	{
		if (m_handle == INVALID_HANDLE_VALUE)
			return nullptr;

		if (!m_first)
		{
			if (!FindNextFileA(m_handle, &m_data))
				return nullptr;
		}
		m_first = false;

		m_entry_storage = make_entry(m_data);
		return m_entry_storage.get();
	}

private:
	static entry::ptr make_entry(const WIN32_FIND_DATAA &fd)
	{
		size_t name_len = std::strlen(fd.cFileName) + 1;
		size_t alloc_size = sizeof(entry) + name_len;
		void *mem = ::operator new(alloc_size, std::align_val_t(alignof(entry)));
		entry *e = new(mem) entry();
		char *name_buf = reinterpret_cast<char *>(e) + sizeof(entry);
		std::memcpy(name_buf, fd.cFileName, name_len);
		e->name = name_buf;
		e->type = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			? entry::entry_type::DIR : entry::entry_type::FILE;
		e->size = (std::uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
		e->last_modified = std::chrono::system_clock::time_point(); // simplified
		return entry::ptr(e);
	}

	HANDLE m_handle;
	WIN32_FIND_DATAA m_data;
	bool m_first;
	entry::ptr m_entry_storage;
};

directory::ptr directory::open(std::string const &dirname)
{
	auto dir = std::make_unique<win32_directory>(dirname);
	return dir;
}

#else // POSIX

class posix_directory : public directory
{
public:
	posix_directory(std::string const &dirname)
		: m_dir(::opendir(dirname.c_str()))
		, m_path(dirname)
	{
	}

	~posix_directory() override
	{
		if (m_dir)
			::closedir(m_dir);
	}

	const entry *read() override
	{
		if (!m_dir)
			return nullptr;

		struct dirent *de = ::readdir(m_dir);
		if (!de)
			return nullptr;

		size_t name_len = std::strlen(de->d_name) + 1;
		size_t alloc_size = sizeof(entry) + name_len;
		void *mem = ::operator new(alloc_size, std::align_val_t(alignof(entry)));
		entry *e = new(mem) entry();
		char *name_buf = reinterpret_cast<char *>(e) + sizeof(entry);
		std::memcpy(name_buf, de->d_name, name_len);
		e->name = name_buf;

		std::string full = m_path + "/" + de->d_name;
		struct stat st;
		if (::stat(full.c_str(), &st) == 0)
		{
			if (S_ISDIR(st.st_mode))
				e->type = entry::entry_type::DIR;
			else if (S_ISREG(st.st_mode))
				e->type = entry::entry_type::FILE;
			else
				e->type = entry::entry_type::OTHER;
			e->size = static_cast<std::uint64_t>(st.st_size);
			e->last_modified = std::chrono::system_clock::from_time_t(st.st_mtime);
		}
		else
		{
			e->type = entry::entry_type::NONE;
			e->size = 0;
			e->last_modified = std::chrono::system_clock::time_point();
		}

		m_entry_storage = entry::ptr(e);
		return m_entry_storage.get();
	}

private:
	DIR *m_dir;
	std::string m_path;
	entry::ptr m_entry_storage;
};

directory::ptr directory::open(std::string const &dirname)
{
	auto dir = std::make_unique<posix_directory>(dirname);
	return dir;
}

#endif

} // namespace osd


//============================================================
//  osd_stat
//============================================================

osd::directory::entry::ptr osd_stat(std::string const &path)
{
#if defined(_WIN32)
	WIN32_FILE_ATTRIBUTE_DATA data;
	if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
		return nullptr;

	size_t alloc_size = sizeof(osd::directory::entry) + 1;
	void *mem = ::operator new(alloc_size, std::align_val_t(alignof(osd::directory::entry)));
	auto *e = new(mem) osd::directory::entry();
	char *name_buf = reinterpret_cast<char *>(e) + sizeof(osd::directory::entry);
	name_buf[0] = '\0';
	e->name = name_buf;
	e->type = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		? osd::directory::entry::entry_type::DIR
		: osd::directory::entry::entry_type::FILE;
	e->size = (std::uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
	e->last_modified = std::chrono::system_clock::time_point();
	return osd::directory::entry::ptr(e);
#else
	struct stat st;
	if (::stat(path.c_str(), &st) < 0)
		return nullptr;

	size_t alloc_size = sizeof(osd::directory::entry) + 1;
	void *mem = ::operator new(alloc_size, std::align_val_t(alignof(osd::directory::entry)));
	auto *e = new(mem) osd::directory::entry();
	char *name_buf = reinterpret_cast<char *>(e) + sizeof(osd::directory::entry);
	name_buf[0] = '\0';
	e->name = name_buf;
	if (S_ISDIR(st.st_mode))
		e->type = osd::directory::entry::entry_type::DIR;
	else if (S_ISREG(st.st_mode))
		e->type = osd::directory::entry::entry_type::FILE;
	else
		e->type = osd::directory::entry::entry_type::OTHER;
	e->size = static_cast<std::uint64_t>(st.st_size);
	e->last_modified = std::chrono::system_clock::from_time_t(st.st_mtime);
	return osd::directory::entry::ptr(e);
#endif
}


//============================================================
//  PATH INTERFACES
//============================================================

bool osd_is_absolute_path(const std::string &path) noexcept
{
	if (path.empty())
		return false;
#if defined(_WIN32)
	// check for drive letter or UNC path
	if (path.size() >= 2 && path[1] == ':')
		return true;
	if (path.size() >= 2 && (path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/'))
		return true;
#endif
	return path[0] == '/' || path[0] == '\\';
}

std::error_condition osd_get_full_path(std::string &dst, std::string const &path) noexcept
{
#if defined(_WIN32)
	char buffer[MAX_PATH];
	if (GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr) == 0)
		return std::error_condition(GetLastError(), std::system_category());
	dst = buffer;
#else
	char *resolved = ::realpath(path.c_str(), nullptr);
	if (resolved)
	{
		dst = resolved;
		std::free(resolved);
	}
	else
	{
		// if path doesn't exist yet, fall back to returning it as-is
		dst = path;
	}
#endif
	return std::error_condition();
}

std::string osd_get_volume_name(int idx)
{
	if (idx == 0)
		return "/";
	return std::string();
}

std::vector<std::string> osd_get_volume_names()
{
	return { "/" };
}
