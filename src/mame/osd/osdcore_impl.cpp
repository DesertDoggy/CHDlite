// license:BSD-3-Clause
// CHDlite OSD core implementation
// Provides implementations for functions declared in osdcore.h

#include "osdcore.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif


//============================================================
//  osd_getenv
//============================================================

const char *osd_getenv(const char *name)
{
	return std::getenv(name);
}


//============================================================
//  osd_getpid
//============================================================

int osd_getpid() noexcept
{
#if defined(_WIN32)
	return static_cast<int>(GetCurrentProcessId());
#else
	return static_cast<int>(getpid());
#endif
}


//============================================================
//  osd_uchar_from_osdchar
//============================================================

// On Windows, this is provided by strconv.cpp for full Unicode support
// On other platforms, use this simple implementation
#if !defined(_WIN32)
int osd_uchar_from_osdchar(char32_t *uchar, const char *osdchar, size_t count)
{
	// simple pass-through for ASCII/UTF-8 single bytes
	if (count >= 1)
		*uchar = static_cast<uint8_t>(*osdchar);
	else
		*uchar = 0;
	return 1;
}
#endif


//============================================================
//  TIMING
//============================================================

osd_ticks_t osd_ticks() noexcept
{
	return std::chrono::steady_clock::now().time_since_epoch().count();
}

osd_ticks_t osd_ticks_per_second() noexcept
{
	using period = std::chrono::steady_clock::period;
	return static_cast<osd_ticks_t>(period::den) / static_cast<osd_ticks_t>(period::num);
}

void osd_sleep(osd_ticks_t duration) noexcept
{
	auto const tps = osd_ticks_per_second();
	if (tps > 0)
	{
		auto const ns = static_cast<uint64_t>(duration) * 1000000000ULL / tps;
		std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
	}
}


//============================================================
//  MISC
//============================================================

void osd_break_into_debugger(const char *message)
{
#if defined(_MSC_VER)
	if (IsDebuggerPresent())
		__debugbreak();
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
	__asm__ __volatile__("int $3");
#elif defined(__GNUC__) && (defined(__arm__) || defined(__aarch64__))
	__builtin_trap();
#endif
	// otherwise do nothing
}

std::pair<std::error_condition, unsigned> osd_get_cache_line_size() noexcept
{
	return std::make_pair(std::error_condition(), 64u); // sane default
}

std::string osd_subst_env(std::string_view src)
{
	std::string result(src);
	// minimal: no env substitution needed for CHDlite
	return result;
}

std::vector<std::string> osd_get_command_line(int argc, char *argv[])
{
	return std::vector<std::string>(argv, argv + argc);
}

void osd_set_aggressive_input_focus(bool aggressive_focus)
{
	// no-op for CHDlite
}


//============================================================
//  OUTPUT
//============================================================

static osd_output *s_output_stack = nullptr;

void osd_output::push(osd_output *delegate)
{
	delegate->m_chain = s_output_stack;
	s_output_stack = delegate;
}

void osd_output::pop(osd_output *delegate)
{
	if (s_output_stack == delegate)
	{
		s_output_stack = delegate->m_chain;
		delegate->m_chain = nullptr;
	}
}

static void osd_output_dispatch(osd_output_channel channel, util::format_argument_pack<char> const &args)
{
	if (s_output_stack)
	{
		s_output_stack->output_callback(channel, args);
	}
	else
	{
		// default: format to stderr for errors/warnings, stdout for info/verbose/debug
		std::string text = util::string_format(args);
		if (channel <= OSD_OUTPUT_CHANNEL_WARNING)
			std::fputs(text.c_str(), stderr);
		else
			std::fputs(text.c_str(), stdout);
	}
}

void osd_vprintf_error(util::format_argument_pack<char> const &args)
{
	osd_output_dispatch(OSD_OUTPUT_CHANNEL_ERROR, args);
}

void osd_vprintf_warning(util::format_argument_pack<char> const &args)
{
	osd_output_dispatch(OSD_OUTPUT_CHANNEL_WARNING, args);
}

void osd_vprintf_info(util::format_argument_pack<char> const &args)
{
	osd_output_dispatch(OSD_OUTPUT_CHANNEL_INFO, args);
}

void osd_vprintf_verbose(util::format_argument_pack<char> const &args)
{
	osd_output_dispatch(OSD_OUTPUT_CHANNEL_VERBOSE, args);
}

void osd_vprintf_debug(util::format_argument_pack<char> const &args)
{
	osd_output_dispatch(OSD_OUTPUT_CHANNEL_DEBUG, args);
}
