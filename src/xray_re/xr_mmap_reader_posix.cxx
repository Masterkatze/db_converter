#include "xr_mmap_reader_posix.hxx"
#include "xr_utils.hxx"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

using namespace xray_re;

xr_mmap_reader_posix::xr_mmap_reader_posix() : xr_reader(nullptr, 0), m_fd(-1), m_file_length(0), m_mem_length(0) {}

xr_mmap_reader_posix::xr_mmap_reader_posix(const std::string& path) : xr_reader()
{
	auto fd = ::open(path.c_str(), O_RDONLY);
	if(fd == -1)
	{
		spdlog::error("Failed to open file \"{}\": {} (errno={}) ", path, strerror(errno), errno);
		throw;
	}

	struct stat sb {};
	if(fstat(fd, &sb) == -1)
	{
		spdlog::error("stat failed for file \"{}\": {} (errno={}) ", path, strerror(errno), errno);
		throw;
	}

	auto file_size = static_cast<std::size_t>(sb.st_size);

	std::size_t mem_size;
	auto page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
	std::size_t remainder = file_size % page_size;

	if(remainder == 0)
	{
		mem_size = file_size;
	}
	else
	{
		mem_size = file_size + page_size - remainder;
	}

	auto data = mmap(nullptr, mem_size, PROT_READ, MAP_PRIVATE, fd, 0);

	if(file_size != 0)
	{
		if(data == MAP_FAILED)
		{
			spdlog::error("mmap failed for file \"{}\": {} (errno={}) ", path, strerror(errno), errno);
			throw;
		}
	}

	m_next = m_p = m_data = static_cast<const uint8_t*>(data);
	m_end = static_cast<const uint8_t*>(data) + file_size;
	m_fd = fd;
	m_file_length = file_size;
	m_mem_length = mem_size;
}

xr_mmap_reader_posix::~xr_mmap_reader_posix()
{
	assert(m_fd != -1);
	assert(m_data);

	if(m_mem_length != 0)
	{
		auto res = madvise(reinterpret_cast<void*>(const_cast<uint8_t*>(m_data)), m_mem_length, MADV_DONTNEED | MADV_FREE);
		if(res == -1)
		{
			spdlog::error("madvise failed: {} (errno={}) ", strerror(errno), errno);
		}

		res = munmap(reinterpret_cast<void*>(const_cast<uint8_t*>(m_data)), m_mem_length);
		if(res != 0)
		{
			spdlog::error("munmap failed with result {}: {} (errno={}) ", res, strerror(errno), errno);
		}
	}

	auto res = posix_fadvise(m_fd, 0, static_cast<off_t>(m_file_length), POSIX_FADV_DONTNEED); //POSIX_FADV_NOREUSE
	if(res != 0)
	{
		spdlog::error("posix_fadvise failed: {} (errno={}) ", strerror(errno), errno);
	}

	res = ::close(m_fd);
	if(res == -1)
	{
		spdlog::error("Failed to close file descriptor {}: {} (errno={}) ", m_fd, strerror(errno), errno);
	}
}
