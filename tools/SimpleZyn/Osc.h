// Osc - a minimal OSC 1.0 sender over UDP (enough to control ZynAddSubFX).
// Types: i (int32), f (float32), s (string), T / F (booleans, no data).
#ifndef OSC_H
#define OSC_H

#include <stddef.h>

// Builds an OSC message into buf. Returns the packet size, or 0 if it does not fit.
// Example: OscBuild(buf, sizeof(buf), "/part0/Pvolume", "i", 100)
size_t OscBuild(char* buf, size_t capacity, const char* path, const char* types, ...);

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

	// Sent datagrams are counted (for the status line and for tests)
	long			SentCount() const { return fSent; }

private:
	int				fSocket;
	int				fPort;
	long			fSent;
	char			fAddress[32];
};

#endif
