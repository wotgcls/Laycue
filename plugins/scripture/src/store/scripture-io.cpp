#include "store/scripture-io.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace laycue::scripture {

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

void addFolder(std::vector<VersionFile> &out, const QString &folder)
{
	const QDir dir(folder);
	for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name)) {
		const std::string file = info.fileName().toStdString();
		if (std::none_of(out.begin(), out.end(), [&](const VersionFile &v) { return v.file == file; }))
			out.push_back({file, info.absoluteFilePath().toStdString()});
	}
}

} // namespace

ScriptureSettings loadSettings()
{
	ScriptureSettings s;
	const std::string path = configPath("scripture.json");
	OBSDataAutoRelease data = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data)
		return s;
	s.version = str(obs_data_get_string(data, "version"));
	s.look = int(obs_data_get_int(data, "look"));
	s.background = str(obs_data_get_string(data, "background"));
	s.lastTyped = str(obs_data_get_string(data, "lastTyped"));
	OBSDataArrayAutoRelease saved = obs_data_get_array(data, "saved");
	const size_t n = saved ? obs_data_array_count(saved) : 0;
	for (size_t i = 0; i < n; ++i) {
		OBSDataAutoRelease item = obs_data_array_item(saved, i);
		const std::string ref = str(obs_data_get_string(item, "ref"));
		if (!ref.empty())
			s.saved.push_back(ref);
	}
	return s;
}

void saveSettings(const ScriptureSettings &s)
{
	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_int(data, "version_schema", 1);
	obs_data_set_string(data, "version", s.version.c_str());
	obs_data_set_int(data, "look", s.look);
	obs_data_set_string(data, "background", s.background.c_str());
	obs_data_set_string(data, "lastTyped", s.lastTyped.c_str());
	OBSDataArrayAutoRelease saved = obs_data_array_create();
	for (const std::string &ref : s.saved) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "ref", ref.c_str());
		obs_data_array_push_back(saved, item);
	}
	obs_data_set_array(data, "saved", saved);
	const std::string path = configPath("scripture.json");
	QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
	obs_data_save_json_safe(data, path.c_str(), "tmp", "bak");
}

std::vector<VersionFile> listVersions()
{
	std::vector<VersionFile> out;
	char *bundled = obs_module_file("bibles");
	if (bundled) {
		addFolder(out, QString::fromUtf8(bundled));
		bfree(bundled);
	}
	addFolder(out, QString::fromStdString(configPath("bibles")));
	return out;
}

std::shared_ptr<const Bible> loadVersion(const VersionFile &version, std::string &error)
{
	static std::map<std::string, std::shared_ptr<const Bible>> cache;
	const auto found = cache.find(version.path);
	if (found != cache.end())
		return found->second;
	auto bible = std::make_shared<Bible>();
	if (!parseBible(readFile(version.path), *bible, error))
		return nullptr;
	cache[version.path] = bible;
	return bible;
}

VersionFile addVersion(const std::string &sourcePath, std::string &error)
{
	Bible check;
	if (!parseBible(readFile(sourcePath), check, error))
		return {};
	const QString folder = QString::fromStdString(configPath("bibles"));
	QDir().mkpath(folder);
	const QString name = QFileInfo(QString::fromStdString(sourcePath)).fileName();
	const QString target = QDir(folder).filePath(name);
	if (QFileInfo(QString::fromStdString(sourcePath)).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
		QFile::remove(target);
		if (!QFile::copy(QString::fromStdString(sourcePath), target)) {
			error = "Laycue couldn't copy the file into its folder.";
			return {};
		}
	}
	return {name.toStdString(), target.toStdString()};
}

} // namespace laycue::scripture
