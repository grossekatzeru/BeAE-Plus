// SmfFile.h - reading of standard MIDI files (format 0 and 1) into one list of timed events.
// Does not depend on Haiku; can be tested anywhere (make test).
#ifndef SMF_FILE_H
#define SMF_FILE_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

struct SmfEvent {
	int64_t	us;			// time from the start of the song, microseconds (at tempo 100%)
	uint8_t	status;		// 0x80..0xEF: command and channel
	uint8_t	d1;
	uint8_t	d2;			// 0 for commands with one data byte
};

// What the file says about one of the 16 channels (for the table of the player)
struct SmfChannel {
	int			notes;			// number of notes (0: the channel is not used)
	int			program;		// the first program change, -1 if there is none
	std::string	name;			// the name of the track that plays only this channel (may be empty)
	SmfChannel() : notes(0), program(-1) {}
};

struct SmfSong {
	std::vector<SmfEvent>	events;		// sorted by time
	SmfChannel				channels[16];
	int64_t					durationUs;
	int						format;
	int						tracks;
	SmfSong() : durationUs(0), format(0), tracks(0) {}
};

// Returns false and fills "error" if the data is not a usable MIDI file.
bool ParseSmf(const uint8_t* data, size_t size, SmfSong& song, std::string& error);

#endif
