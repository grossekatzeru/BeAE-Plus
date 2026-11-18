#include "Engine.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


Engine::Engine()
	:
	fPid(-1),
	fPipe(-1),
	fPort(0)
{
}


Engine::~Engine()
{
	Stop();
}


bool
Engine::Start(const char* program)
{
	Stop();

	int fds[2];
	if (pipe(fds) != 0)
		return false;

	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return false;
	}
	if (pid == 0) {
		// child: the engine writes its messages to the pipe (stdout and stderr)
		close(fds[0]);
		dup2(fds[1], STDERR_FILENO);
		dup2(fds[1], STDOUT_FILENO);
		close(fds[1]);
		execlp(program, program, "--no-gui", (char*)NULL);
		fprintf(stderr, "SimpleZyn: cannot start %s: %s\n", program, strerror(errno));
		_exit(127);
	}

	close(fds[1]);
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
	fPid = pid;
	fPipe = fds[0];
	fPort = 0;
	fPending.clear();
	return true;
}


void
Engine::Stop()
{
	if (fPid > 0) {
		kill(fPid, SIGTERM);
		int status;
		// give it a moment to close the audio output, then force it
		for (int i = 0; i < 30; i++) {
			if (waitpid(fPid, &status, WNOHANG) == fPid) {
				fPid = -1;
				break;
			}
			usleep(100000);
		}
		if (fPid > 0) {
			kill(fPid, SIGKILL);
			waitpid(fPid, &status, 0);
			fPid = -1;
		}
	}
	if (fPipe >= 0)
		close(fPipe);
	fPipe = -1;
	fPort = 0;
}


bool
Engine::Poll()
{
	if (fPipe < 0)
		return false;

	char buf[1024];
	for (;;) {
		ssize_t n = read(fPipe, buf, sizeof(buf));
		if (n <= 0)
			break;
		fwrite(buf, 1, (size_t)n, stderr);			// what the engine says goes to our Terminal
		fPending.append(buf, (size_t)n);
	}

	if (fPort == 0) {
		const char* key = "lo server running on ";
		size_t at = fPending.find(key);
		if (at != std::string::npos) {
			int port = atoi(fPending.c_str() + at + strlen(key));
			if (port > 0)
				fPort = port;
		}
	}
	if (fPending.size() > 4096)
		fPending.erase(0, fPending.size() - 1024);
	return fPort > 0;
}


bool
Engine::HasExited()
{
	if (fPid <= 0)
		return false;
	int status;
	if (waitpid(fPid, &status, WNOHANG) == fPid) {
		fPid = -1;
		return true;
	}
	return false;
}
