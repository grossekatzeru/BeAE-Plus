#include "Osc.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>


static size_t
Padded(size_t n)
{
	return (n + 4) & ~(size_t)3;		// string with a terminating zero, padded to 4 bytes
}


static size_t
PutString(char* buf, size_t capacity, size_t at, const char* s)
{
	const size_t size = Padded(strlen(s));
	if (at + size > capacity)
		return 0;
	memset(buf + at, 0, size);
	memcpy(buf + at, s, strlen(s));
	return size;
}


static size_t
VBuild(char* buf, size_t capacity, const char* path, const char* types, va_list args)
{
	size_t at = 0;
	size_t n = PutString(buf, capacity, at, path);
	if (n == 0)
		return 0;
	at += n;

	// type tag string: "," followed by the types
	char tags[64];
	tags[0] = ',';
	size_t count = strlen(types);
	if (count > sizeof(tags) - 2)
		return 0;
	memcpy(tags + 1, types, count);
	tags[count + 1] = 0;
	n = PutString(buf, capacity, at, tags);
	if (n == 0)
		return 0;
	at += n;

	for (size_t i = 0; i < count; i++) {
		switch (types[i]) {
			case 'i': {
				if (at + 4 > capacity)
					return 0;
				uint32_t v = htonl((uint32_t)va_arg(args, int));
				memcpy(buf + at, &v, 4);
				at += 4;
				break;
			}
			case 'f': {
				if (at + 4 > capacity)
					return 0;
				float f = (float)va_arg(args, double);
				uint32_t v;
				memcpy(&v, &f, 4);
				v = htonl(v);
				memcpy(buf + at, &v, 4);
				at += 4;
				break;
			}
			case 's': {
				n = PutString(buf, capacity, at, va_arg(args, const char*));
				if (n == 0)
					return 0;
				at += n;
				break;
			}
			case 'T':
			case 'F':
				break;					// no data
			default:
				return 0;
		}
	}
	return at;
}


size_t
OscBuild(char* buf, size_t capacity, const char* path, const char* types, ...)
{
	va_list args;
	va_start(args, types);
	size_t size = VBuild(buf, capacity, path, types, args);
	va_end(args);
	return size;
}


OscClient::OscClient()
	:
	fSocket(-1),
	fPort(0),
	fSent(0)
{
	fAddress[0] = 0;
}


OscClient::~OscClient()
{
	Close();
}


bool
OscClient::Open(const char* host, int port)
{
	Close();
	fSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (fSocket < 0)
		return false;

	struct sockaddr_in to;
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, host, &to.sin_addr) != 1) {
		Close();
		return false;
	}
	// connect() on UDP only remembers the destination, so send() can be used
	if (connect(fSocket, (struct sockaddr*)&to, sizeof(to)) != 0) {
		Close();
		return false;
	}
	fPort = port;
	strncpy(fAddress, host, sizeof(fAddress) - 1);
	fAddress[sizeof(fAddress) - 1] = 0;
	return true;
}


void
OscClient::Close()
{
	if (fSocket >= 0)
		close(fSocket);
	fSocket = -1;
	fPort = 0;
}


bool
OscClient::Send(const char* path, const char* types, ...)
{
	if (fSocket < 0)
		return false;
	char buf[1024];
	va_list args;
	va_start(args, types);
	size_t size = VBuild(buf, sizeof(buf), path, types, args);
	va_end(args);
	if (size == 0)
		return false;
	if (send(fSocket, buf, size, 0) != (ssize_t)size)
		return false;
	fSent++;
	return true;
}


static bool
ReadString(const char* buf, size_t size, size_t& at, std::string& out)
{
	size_t end = at;
	while (end < size && buf[end] != 0)
		end++;
	if (end >= size)
		return false;					// no terminating zero
	out.assign(buf + at, end - at);
	at += Padded(end - at);
	return at <= size;
}


bool
OscParse(const char* buf, size_t size, OscMessage& out)
{
	out.path.clear();
	out.args.clear();
	size_t at = 0;
	if (size < 4 || buf[0] != '/')
		return false;					// also rejects "#bundle"
	if (!ReadString(buf, size, at, out.path))
		return false;
	if (at >= size)
		return true;					// old style message without type tags
	std::string tags;
	if (!ReadString(buf, size, at, tags) || tags.empty() || tags[0] != ',')
		return false;
	for (size_t t = 1; t < tags.size(); t++) {
		OscArg a;
		a.type = tags[t];
		a.i = 0;
		a.f = 0;
		switch (a.type) {
			case 'i': {
				if (at + 4 > size)
					return false;
				uint32_t v;
				memcpy(&v, buf + at, 4);
				a.i = (int)ntohl(v);
				at += 4;
				break;
			}
			case 'f': {
				if (at + 4 > size)
					return false;
				uint32_t v;
				memcpy(&v, buf + at, 4);
				v = ntohl(v);
				memcpy(&a.f, &v, 4);
				at += 4;
				break;
			}
			case 's':
				if (!ReadString(buf, size, at, a.s))
					return false;
				break;
			case 'T':
				a.i = 1;
				break;
			case 'F':
				a.i = 0;
				break;
			default:
				return false;
		}
		out.args.push_back(a);
	}
	return true;
}


bool
OscClient::Receive(OscMessage& out, int timeoutMs)
{
	if (fSocket < 0)
		return false;
	struct pollfd p;
	p.fd = fSocket;
	p.events = POLLIN;
	p.revents = 0;
	if (poll(&p, 1, timeoutMs) <= 0 || !(p.revents & POLLIN))
		return false;
	char buf[2048];
	ssize_t n = recv(fSocket, buf, sizeof(buf), 0);
	if (n <= 0)
		return false;
	return OscParse(buf, (size_t)n, out);
}
