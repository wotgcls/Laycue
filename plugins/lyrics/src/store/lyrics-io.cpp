#include "store/lyrics-io.hpp"

#include <obs-module.h>
#include <obs.hpp>

#include <QDir>
#include <QFileInfo>

#include <fstream>
#include <sstream>

namespace laycue::lyrics {

namespace {

std::string str(const char *s)
{
	return s ? std::string(s) : std::string();
}

std::string configPath(const char *file)
{
	char *p = obs_module_config_path(file);
	const std::string out = str(p);
	bfree(p);
	return out;
}

std::string readFile(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

void saveJson(obs_data_t *data, const std::string &path)
{
	QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
	obs_data_save_json_safe(data, path.c_str(), "tmp", "bak");
}

std::vector<std::string> idsOf(const std::vector<Song> &songs)
{
	std::vector<std::string> ids;
	for (const Song &s : songs)
		ids.push_back(s.id);
	return ids;
}

} // namespace

LyricsSettings loadSettings()
{
	LyricsSettings s;
	OBSDataAutoRelease data = obs_data_create_from_json_file_safe(configPath("lyrics.json").c_str(), "bak");
	if (!data)
		return s;
	s.look = int(obs_data_get_int(data, "look"));
	s.background = str(obs_data_get_string(data, "background"));
	s.lastSong = str(obs_data_get_string(data, "lastSong"));
	return s;
}

void saveSettings(const LyricsSettings &s)
{
	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_int(data, "look", s.look);
	obs_data_set_string(data, "background", s.background.c_str());
	obs_data_set_string(data, "lastSong", s.lastSong.c_str());
	saveJson(data, configPath("lyrics.json"));
}

std::vector<Song> loadLibrary()
{
	std::vector<Song> songs;
	OBSDataAutoRelease data = obs_data_create_from_json_file_safe(configPath("songs.json").c_str(), "bak");
	if (data) {
		OBSDataArrayAutoRelease array = obs_data_get_array(data, "songs");
		const size_t n = array ? obs_data_array_count(array) : 0;
		for (size_t i = 0; i < n; ++i) {
			OBSDataAutoRelease item = obs_data_array_item(array, i);
			Song song = parseSongText(str(obs_data_get_string(item, "title")),
						  str(obs_data_get_string(item, "author")),
						  str(obs_data_get_string(item, "text")));
			song.id = str(obs_data_get_string(item, "id"));
			if (song.id.empty())
				song.id = makeSongId(song.title, idsOf(songs));
			if (!song.title.empty())
				songs.push_back(song);
		}
		return songs;
	}
	// First start: the bundled hymns.
	char *folder = obs_module_file("songs");
	if (folder) {
		const QDir dir(QString::fromUtf8(folder));
		bfree(folder);
		for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name)) {
			Song song = parseSongFile(readFile(info.absoluteFilePath().toStdString()),
						  info.completeBaseName().toStdString());
			song.id = makeSongId(song.title, idsOf(songs));
			if (!song.sections.empty())
				songs.push_back(song);
		}
	}
	saveLibrary(songs);
	return songs;
}

void saveLibrary(const std::vector<Song> &songs)
{
	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_int(data, "version", 1);
	OBSDataArrayAutoRelease array = obs_data_array_create();
	for (const Song &s : songs) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "id", s.id.c_str());
		obs_data_set_string(item, "title", s.title.c_str());
		obs_data_set_string(item, "author", s.author.c_str());
		obs_data_set_string(item, "text", songText(s).c_str());
		obs_data_array_push_back(array, item);
	}
	obs_data_set_array(data, "songs", array);
	saveJson(data, configPath("songs.json"));
}

bool importSongFile(const std::string &path, Song &out, std::string &error)
{
	const std::string text = readFile(path);
	if (text.empty()) {
		error = "The file is empty or can't be read.";
		return false;
	}
	const QFileInfo info(QString::fromStdString(path));
	if (info.suffix().compare(QStringLiteral("xml"), Qt::CaseInsensitive) == 0)
		return parseOpenLyrics(text, out, error);
	out = parseSongFile(text, info.completeBaseName().toStdString());
	if (out.sections.empty()) {
		error = "There are no lyrics in this file.";
		return false;
	}
	return true;
}

} // namespace laycue::lyrics
