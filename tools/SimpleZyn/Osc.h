// Osc - a minimal OSC 1.0 sender over UDP (enough to control ZynAddSubFX).
// Types: i (int32), f (float32), s (string), T / F (booleans, no data).
#ifndef OSC_H
#define OSC_H

#include <stddef.h>
#include <string>
#include <vector>

// Builds an OSC message into buf. Returns the packet size, or 0 if it does not fit.
// Example: OscBuild(buf, sizeof(buf), "/part0/Pvolume", "i", 100)
size_t OscBuild(char* buf, size_t capacity, const char* path, const char* types, ...);

// A received message (an answer of the engine)
struct OscArg {
	char		type;			// 'i', 'f', 's', 'T' or 'F'
	int			i;
	float		f;
	std::string	s;
};

struct OscMessage {
	std::string				path;
	std::vector<OscArg>		args;
};

// Decodes one OSC message. Returns false for bundles, broken packets and unknown argument types.
bool OscParse(const char* buf, size_t size, OscMessage& out);

class OscClient {
public:
					OscClient();
					~OscClient();

	// Opens a UDP socket aimed at host:port (host is an IPv4 address, e.g. "127.0.0.1").
	bool			Open(const char* host, int port);
	void			Close();
	bool			IsOpen() const { return fSocket >= 0; }
	int				Port() const { return fPort; }

	bool			Send(const char* path, const char* types, ...);

	// Waits up to timeoutMs for an answer (the engine answers to the address the request came from).
	// 0 = only what has arrived already. Returns false if nothing (decodable) arrived.
	bool			Receive(OscMessage& out, int timeoutMs);

	// Sent datagrams are counted (for the status line and for tests)
	long			SentCount() const { return fSent; }

private:
	int				fSocket;
	int				fPort;
	long			fSent;
	char			fAddress[32];
};

#endif
