/*
 * Внешние кодировщики для сжатых форматов («Сохранить как...» / «Миксдаун как...», формат «Сжатый»).
 * Ядро без зависимостей от Haiku (проверяется на Linux: tests/encoder/run_tests.sh):
 *   - профили кодировщиков (lame, oggenc, flac, opusenc): какая программа, расширение, какие настройки есть;
 *   - поиск программы в PATH и в типичных папках Haiku;
 *   - настройки <-> строка (хранятся в настройках программы);
 *   - сборка командной строки (без оболочки: имена с пробелами и русскими буквами безопасны);
 *   - запись временного 16/24-битного WAV из float;
 *   - запуск программы с захватом вывода и отменой.
 */
#ifndef EXT_ENCODER_H
#define EXT_ENCODER_H

#include <string>
#include <vector>

namespace ExtEncoder {

enum Id { MP3 = 0, OGG, FLAC, OPUS, COUNT };

// Режим кодирования (не у всех форматов): VBR - переменный, CBR - постоянный, ABR - средний (только lame).
enum Mode { VBR = 0, CBR = 1, ABR = 2 };
enum Stereo { JOINT = 0, STEREO = 1, MONO = 2 };

struct Tags {
	std::string title, artist, album, year, comment;
	bool Empty() const { return title.empty() && artist.empty() && album.empty() && year.empty() && comment.empty(); }
};

struct Settings {
	int mode;			// Mode
	int quality;		// lame: V 0..9 (0 лучшее); oggenc: 0..10 (10 лучшее); для остальных не используется
	int bitrate;		// кбит/с для CBR/ABR (lame), для режима «битрейт» (oggenc) и opusenc
	int stereo;			// Stereo (только lame)
	int level;			// flac: уровень сжатия 0..8
	Tags tags;
};

struct Profile {
	Id id;
	const char *key;			// короткое имя в настройках: "mp3"
	const char *title;			// как показывается в списке кодеков
	const char *program;		// имя исполняемого файла
	const char *extension;		// расширение результата без точки
	int wavBits;				// какую разрядность временного WAV отдавать кодировщику
	bool hasMode, hasQuality, hasBitrate, hasStereo, hasLevel;
};

const Profile &GetProfile(Id id);

// Настройки по умолчанию для кодека («lame -V2», oggenc -q 5, flac -5, opus 128 кбит/с).
Settings Defaults(Id id);
// Приведение к допустимым границам.
void Clamp(Id id, Settings &s);
// Строка вида "mode=0;q=2;br=192;st=0;lv=5;tag_artist=..." (только значения, поддерживаемые кодеком) и обратно.
std::string ToString(Id id, const Settings &s);
Settings FromString(Id id, const std::string &str);

// Полный путь к программе или пустая строка. extraDirs - дополнительные папки поиска (кроме PATH и стандартных).
std::string FindProgram(const char *program, const std::vector<std::string> &extraDirs = std::vector<std::string>());
// Найденные в системе кодировщики (в порядке MP3, OGG, FLAC, OPUS).
std::vector<Id> Available(const std::vector<std::string> &extraDirs = std::vector<std::string>());

// Командная строка: args[0] - путь к программе. Теги из s.tags; title пустой -> берётся fallbackTitle.
std::vector<std::string> BuildArgs(Id id, const std::string &program, const Settings &s, const std::string &fallbackTitle,
	const std::string &inWav, const std::string &outFile);

// Временный WAV (PCM, 16 или 24 бита) из чередующихся float. 16 бит - с лёгким треугольным дизерингом.
// progress(доля 0..1, user) вызывается по ходу; верните false - прервать. Возвращает false при ошибке/отмене.
typedef bool (*ProgressFn)(double fraction, void *user);
bool WriteWavPcm(const char *path, const float *data, long long frames, int channels, float rate, int bits,
	ProgressFn progress = 0, void *user = 0);

// Запуск программы без оболочки. output - объединённые stdout и stderr (последние ~4 КБ).
// cancel() периодически опрашивается; true - прервать (процесс убивается). Возвращает код завершения, -1 не удалось
// запустить, -2 отменено.
typedef bool (*CancelFn)(void *user);
int Run(const std::vector<std::string> &args, std::string &output, CancelFn cancel = 0, void *user = 0);

// Целиком: пишет временный WAV (во временную папку tempDir), запускает кодировщик, удаляет временный файл.
// err - понятное сообщение при неудаче (текст ошибки программы). outFile - итоговый путь (расширение уже должно быть).
bool Encode(Id id, const Settings &s, const std::string &tempDir, const std::string &fallbackTitle,
	const float *data, long long frames, int channels, float rate, const std::string &outFile, std::string &err,
	ProgressFn progress = 0, CancelFn cancel = 0, void *user = 0,
	const std::vector<std::string> &extraDirs = std::vector<std::string>());

}	// namespace ExtEncoder

#endif
