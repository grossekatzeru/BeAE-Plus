// Engine - starts the ZynAddSubFX engine without its own window and finds out its OSC port.
#ifndef ENGINE_H
#define ENGINE_H

#include <string>
#include <sys/types.h>

class Engine {
public:
				Engine();
				~Engine();

	// Starts "<program> --no-gui". The engine prints "lo server running on <port>" to stderr;
	// Poll() reads it. Returns false if the program could not be started.
	bool		Start(const char* program);
	void		Stop();

	// Call regularly (for example every 200 ms): reads the engine's output (it is copied to our
	// stderr, so a Terminal shows what the engine says) and returns true once the port is known.
	bool		Poll();

	bool		IsRunning() const { return fPid > 0; }
	bool		HasPort() const { return fPort > 0; }
	int			Port() const { return fPort; }

	// Did the child process exit by itself?
	bool		HasExited();

private:
	pid_t		fPid;
	int			fPipe;
	int			fPort;
	std::string	fPending;
};

#endif
