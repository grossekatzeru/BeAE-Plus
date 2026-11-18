// Checks the portable part of SimpleZyn on any system:  make test
// - OSC messages are decoded again and compared with what ZynControl should send
// - the bank scanner and the name cleaner
// - the engine launcher (with a fake engine script that prints the OSC port)
#include "Banks.h"
#include "Engine.h"
#include "Osc.h"
#include "ZynControl.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)


// decode one OSC message into "path types args..." text
static std::string
Decode(const char* buf, size_t size)
{
	std::string out;
	size_t at = 0;
	const char* path = buf;
	out += path;
	at = (strlen(path) + 4) & ~(size_t)3;
	const char* tags = buf + at;
	at += (strlen(tags) + 4) & ~(size_t)3;
	for (const char* t = tags + 1; *t; t++) {
		out += " ";
		if (*t == 'i') {
			uint32_t v;
			memcpy(&v, buf + at, 4);
			at += 4;
			out += std::to_string((int)ntohl(v));
		} else if (*t == 's') {
			out += buf + at;
			at += (strlen(buf + at) + 4) & ~(size_t)3;
		} else if (*t == 'T') {
			out += "T";
		} else if (*t == 'F') {
			out += "F";
		}
	}
	if (at != size)
		out += " [SIZE MISMATCH]";
	return out;
}


static int
OpenServer(int* port)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = 0;
	bind(s, (struct sockaddr*)&a, sizeof(a));
	socklen_t len = sizeof(a);
	getsockname(s, (struct sockaddr*)&a, &len);
	*port = ntohs(a.sin_port);
	struct timeval tv = {1, 0};
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	return s;
}


static std::string
Receive(int s)
{
	char buf[1024];
	ssize_t n = recv(s, buf, sizeof(buf), 0);
	if (n <= 0)
		return "(nothing)";
	return Decode(buf, (size_t)n);
}


int
main()
{
	// --- OSC encoding against known byte layouts
	{
		char buf[64];
		size_t n = OscBuild(buf, sizeof(buf), "/a", "i", 1);
		const char expect[] = {'/', 'a', 0, 0, ',', 'i', 0, 0, 0, 0, 0, 1};
		CHECK(n == sizeof(expect) && memcmp(buf, expect, n) == 0);
		n = OscBuild(buf, sizeof(buf), "/part0/Penabled", "T");
		CHECK(n == 16 + 4);						// 15+1 padded path, ",T" padded
		n = OscBuild(buf, 8, "/too/long/path", "i", 1);
		CHECK(n == 0);
	}

	// --- ZynControl messages
	{
		int port;
		int s = OpenServer(&port);
		ZynControl z;
		CHECK(z.Connect(port));
		z.Setup();
		CHECK(Receive(s) == "/sysefx0/efftype 1");
		z.LoadInstrument(2, "/x/bank/0001-Pad.xiz");
		CHECK(Receive(s) == "/load_xiz 2 /x/bank/0001-Pad.xiz");
		CHECK(Receive(s) == "/part2/Penabled T");
		z.SetVolume(3, 200);
		CHECK(Receive(s) == "/part3/Pvolume 127");
		z.SetPan(3, -5);
		CHECK(Receive(s) == "/part3/Ppanning 0");
		z.SetChannel(15, 9);
		CHECK(Receive(s) == "/part15/Prcvchn 9");
		z.SetMuted(1, true);
		CHECK(Receive(s) == "/part1/Pnoteon F");
		z.SetMasterVolume(80);
		CHECK(Receive(s) == "/Pvolume 80");
		z.SetReverb(40);
		CHECK(Receive(s) == "/Psysefxvol0/part0 40");
		for (int i = 1; i < 16; i++)
			Receive(s);
		z.RecordStart("/tmp/out.wav");
		CHECK(Receive(s) == "/HDDRecorder/preparefile /tmp/out.wav");
		CHECK(Receive(s) == "/HDDRecorder/start");
		z.RecordStop();
		CHECK(Receive(s) == "/HDDRecorder/stop");
		close(s);
	}

	// --- bank scanner
	{
		CHECK(InstrumentName("0012-Warm_Pad.xiz") == "Warm Pad");
		CHECK(InstrumentName("Plain.xiz") == "Plain");
		CHECK(InstrumentName("12-3.xiz") == "3");
		CHECK(NameMatches("Warm Pad", "warm"));
		CHECK(!NameMatches("Warm Pad", "bass"));

		system("rm -rf /tmp/sz_banks && mkdir -p /tmp/sz_banks/BankB /tmp/sz_banks/BankA /tmp/sz_banks/Empty");
		system("touch /tmp/sz_banks/BankA/0002-Second.xiz /tmp/sz_banks/BankA/0001-First_One.xiz /tmp/sz_banks/BankA/notes.txt");
		system("touch /tmp/sz_banks/BankB/0001-Only.XIZ");
		std::vector<std::string> roots;
		roots.push_back("/tmp/sz_banks");
		roots.push_back("/tmp/sz_banks/nonexistent");
		std::vector<Bank> banks;
		ScanBanks(roots, banks);
		CHECK(banks.size() == 2);
		if (banks.size() == 2) {
			CHECK(banks[0].name == "BankA" && banks[1].name == "BankB");
			CHECK(banks[0].instruments.size() == 2);
			CHECK(banks[0].instruments[0].name == "First One");
			CHECK(banks[0].instruments[1].name == "Second");
			CHECK(banks[1].instruments.size() == 1);
		}
	}

	// --- engine launcher with a fake engine
	{
		FILE* f = fopen("/tmp/sz_fake_engine.sh", "w");
		fputs("#!/bin/sh\necho 'starting fake engine' >&2\nsleep 0.3\necho 'lo server running on 43210' >&2\nsleep 30\n", f);
		fclose(f);
		chmod("/tmp/sz_fake_engine.sh", 0755);

		Engine e;
		CHECK(e.Start("/tmp/sz_fake_engine.sh"));
		bool got = false;
		for (int i = 0; i < 40 && !got; i++) {
			got = e.Poll();
			usleep(100000);
		}
		CHECK(got && e.Port() == 43210);
		e.Stop();
		CHECK(!e.IsRunning());

		Engine missing;
		CHECK(missing.Start("/nonexistent/engine"));		// the fork succeeds, the child exits
		bool exited = false;
		for (int i = 0; i < 20 && !exited; i++) {
			missing.Poll();
			exited = missing.HasExited();
			usleep(100000);
		}
		CHECK(exited);
	}

	printf(failures ? "FAILED: %d\n" : "all tests passed\n", failures);
	return failures ? 1 : 0;
}
