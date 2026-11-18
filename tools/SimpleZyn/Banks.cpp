#include "Banks.h"

#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>


static bool
EndsWith(const std::string& s, const char* suffix)
{
	const std::string x(suffix);
	if (s.size() < x.size())
		return false;
	for (size_t i = 0; i < x.size(); i++) {
		if (tolower((unsigned char)s[s.size() - x.size() + i]) != tolower((unsigned char)x[i]))
			return false;
	}
	return true;
}


static bool
IsDirectory(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}


std::string
InstrumentName(const std::string& fileName)
{
	std::string n = fileName;
	if (EndsWith(n, ".xiz"))
		n.erase(n.size() - 4);
	size_t i = 0;
	while (i < n.size() && isdigit((unsigned char)n[i]))
		i++;
	if (i > 0 && i < n.size() && n[i] == '-')
		n.erase(0, i + 1);
	for (size_t k = 0; k < n.size(); k++) {
		if (n[k] == '_')
			n[k] = ' ';
	}
	return n;
}


bool
NameMatches(const std::string& name, const std::string& pattern)
{
	if (pattern.empty())
		return true;
	std::string a(name), b(pattern);
	for (size_t i = 0; i < a.size(); i++)
		a[i] = (char)tolower((unsigned char)a[i]);
	for (size_t i = 0; i < b.size(); i++)
		b[i] = (char)tolower((unsigned char)b[i]);
	return a.find(b) != std::string::npos;
}


static bool
ByInstrumentFile(const Instrument& a, const Instrument& b)
{
	return a.file < b.file;			// the leading numbers keep the bank's own order
}


static bool
ByBankName(const Bank& a, const Bank& b)
{
	return a.name < b.name;
}


static void
ReadBank(const std::string& path, const std::string& name, std::vector<Bank>& banks)
{
	DIR* dir = opendir(path.c_str());
	if (dir == NULL)
		return;
	Bank bank;
	bank.name = name;
	bank.path = path;
	struct dirent* e;
	while ((e = readdir(dir)) != NULL) {
		const std::string file(e->d_name);
		if (file[0] == '.' || !EndsWith(file, ".xiz"))
			continue;
		Instrument ins;
		ins.name = InstrumentName(file);
		ins.file = path + "/" + file;
		bank.instruments.push_back(ins);
	}
	closedir(dir);
	if (bank.instruments.empty())
		return;
	std::sort(bank.instruments.begin(), bank.instruments.end(), ByInstrumentFile);
	banks.push_back(bank);
}


void
ScanBanks(const std::vector<std::string>& roots, std::vector<Bank>& banks)
{
	banks.clear();
	for (size_t r = 0; r < roots.size(); r++) {
		const std::string& root = roots[r];
		DIR* dir = opendir(root.c_str());
		if (dir == NULL)
			continue;
		bool hasFiles = false;
		std::vector<std::string> subdirs;
		struct dirent* e;
		while ((e = readdir(dir)) != NULL) {
			const std::string file(e->d_name);
			if (file[0] == '.')
				continue;
			const std::string full = root + "/" + file;
			if (EndsWith(file, ".xiz"))
				hasFiles = true;
			else if (IsDirectory(full))
				subdirs.push_back(file);
		}
		closedir(dir);

		if (hasFiles) {
			size_t slash = root.find_last_of('/');
			ReadBank(root, slash == std::string::npos ? root : root.substr(slash + 1), banks);
		}
		for (size_t i = 0; i < subdirs.size(); i++)
			ReadBank(root + "/" + subdirs[i], subdirs[i], banks);
	}
	std::sort(banks.begin(), banks.end(), ByBankName);
}
