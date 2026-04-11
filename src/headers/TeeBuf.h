#ifndef TEEBUF_H
#define TEEBUF_H

#include <iostream>
#include <fstream>
#include <streambuf>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdio>

class TeeTimestampBuf : public std::streambuf
{
public:
	TeeTimestampBuf(std::streambuf *consoleBuf, std::streambuf *fileBuf)
		: consoleBuf(consoleBuf),
		  fileBuf(fileBuf),
		  atLineStartFile(true),
		  start(std::chrono::steady_clock::now())
	{
	}

	~TeeTimestampBuf() override = default;

protected:
	// Write one character
	virtual int overflow(int c) override
	{
		if (c == EOF)
			return !EOF;

		std::lock_guard<std::mutex> lock(mtx);

		// write to console (unchanged)
		if (consoleBuf->sputc(c) == EOF)
			return EOF;

		// write to file with timestamp insertion at line starts
		return putCharToFile(static_cast<char>(c));
	}

	// Write a block of characters
	virtual std::streamsize xsputn(const char *s, std::streamsize n) override
	{
		std::lock_guard<std::mutex> lock(mtx);

		// Write full block to console unchanged
		const std::streamsize rConsole = consoleBuf->sputn(s, n);
		if (rConsole == EOF)
			return 0;

		// Write to file while inserting timestamp prefixes at line starts
		std::streamsize written = 0;
		const char *ptr = s;
		const char *end = s + n;

		while (ptr < end)
		{
			if (atLineStartFile)
			{
				const std::string ts = makeTimestampPrefix();
				if (fileBuf->sputn(ts.data(), (std::streamsize)ts.size()) < (std::streamsize)ts.size())
				{
					return written;
				}
				atLineStartFile = false;
			}

			// find next newline (include it in this chunk)
			const char *nextNewline = static_cast<const char *>(memchr(ptr, '\n', end - ptr));
			if (nextNewline)
			{
				// write up to and including newline
				std::streamsize chunkLen = (nextNewline - ptr) + 1;
				if (fileBuf->sputn(ptr, chunkLen) < chunkLen)
					return written;
				ptr += chunkLen;
				written += chunkLen;
				atLineStartFile = true; // next char will be start of a new line
			}
			else
			{
				// no newline in remainder, write all
				std::streamsize chunkLen = end - ptr;
				if (chunkLen > 0)
				{
					if (fileBuf->sputn(ptr, chunkLen) < chunkLen)
						return written;
					written += chunkLen;
				}
				ptr = end;
			}
		}

		return n;
	}

	virtual int sync() override
	{
		std::lock_guard<std::mutex> lock(mtx);
		const int r1 = consoleBuf->pubsync();
		const int r2 = fileBuf->pubsync();
		return (r1 == 0 && r2 == 0) ? 0 : -1;
	}

private:
	std::streambuf *consoleBuf;
	std::streambuf *fileBuf;
	std::mutex mtx;
	bool atLineStartFile;
	std::chrono::steady_clock::time_point start;

	// Helper to write single char to file and manage atLineStartFile
	int putCharToFile(char c)
	{
		if (atLineStartFile)
		{
			const std::string ts = makeTimestampPrefix();
			if (fileBuf->sputn(ts.data(), (std::streamsize)ts.size()) < (std::streamsize)ts.size())
				return EOF;
			atLineStartFile = false;
		}
		if (fileBuf->sputc(c) == EOF)
			return EOF;
		if (c == '\n')
			atLineStartFile = true;
		return static_cast<unsigned char>(c);
	}

	// Format timestamp relative to start: "[<sec>.<ms> s] "
	std::string makeTimestampPrefix()
	{
		using namespace std::chrono;
		auto now = steady_clock::now();
		auto elapsed = now - start;
		auto msTotal = duration_cast<milliseconds>(elapsed).count();
		long long seconds = msTotal / 1000;
		int millisecondsPart = (int)(msTotal % 1000);

		// Format like: [0.123s] (pad milliseconds to 3 digits)
		char buf[64];
		std::snprintf(buf, sizeof(buf), "[%lld.%03ds] ", seconds, millisecondsPart);
		return std::string(buf);
	}
};

#endif // TEEBUF_H
