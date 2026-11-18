// Banks - finds ZynAddSubFX instruments (.xiz files) in bank folders.
#ifndef BANKS_H
#define BANKS_H

#include <string>
#include <vector>

struct Instrument {
	std::string	name;		// "Bass 1" (without the leading number and the extension)
	std::string	file;		// full path
};

struct Bank {
	std::string	name;		// folder name
	std::string	path;
	std::vector<Instrument>	instruments;
};

// A root folder contains bank folders, each with .xiz files. Roots are scanned one level deep;
// a root that itself contains .xiz files is treated as one bank.
void ScanBanks(const std::vector<std::string>& roots, std::vector<Bank>& banks);

// "0012-Warm_Pad.xiz" -> "Warm Pad"
std::string InstrumentName(const std::string& fileName);

// Case-insensitive substring test (for the search box)
bool NameMatches(const std::string& name, const std::string& pattern);

#endif
