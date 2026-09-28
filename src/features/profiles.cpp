#include "features/profiles.hpp"

#include "features/app.hpp"
#include "obs/overlay-items.hpp"
#include "obs/scene-reader.hpp"
#include "store/config-io.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDir>
#include <QFile>
#include <QString>

#include <algorithm>

namespace laycue {

namespace {

std::string profileFolder()
{
	char *path = obs_module_config_path("profiles");
	if (!path)
		return std::string();
	const std::string folder = path;
	bfree(path);
	QDir().mkpath(QString::fromStdString(folder));
	return folder;
}

// A name that is safe as a file name.
std::string fileSafe(const std::string &name)
{
	std::string safe;
	for (char c : name) {
		if (std::string("\\/:*?\"<>|").find(c) != std::string::npos)
			continue;
		safe += c;
	}
	// Trim spaces and dots at the ends.
	while (!safe.empty() && (safe.back() == ' ' || safe.back() == '.'))
		safe.pop_back();
	while (!safe.empty() && safe.front() == ' ')
		safe.erase(safe.begin());
	return safe;
}

std::string profilePath(const std::string &name)
{
	const std::string folder = profileFolder();
	const std::string safe = fileSafe(name);
	if (folder.empty() || safe.empty())
		return std::string();
	return folder + "/" + safe + ".json";
}

// The setup as it can travel: sources and scenes by name, no pushbacks.
Config makePortable(const Config &config)
{
	Config portable = config;
	portable.pushbacks.clear();
	portable.dismissedTips.clear();
	for (TagAssignment &tag : portable.tags)
		tag.sourceUuid.clear();

	std::vector<std::pair<std::string, std::string>> scenes; // uuid, name
	for (const SceneInfo &scene : readSnapshot().scenes)
		scenes.emplace_back(scene.uuid, scene.name);
	for (Rule &rule : portable.rules) {
		std::vector<std::string> names;
		for (const std::string &uuid : rule.scenes) {
			const auto found = std::find_if(scenes.begin(), scenes.end(),
							[&](const auto &s) { return s.first == uuid; });
			if (found != scenes.end())
				names.push_back(found->second);
		}
		rule.scenes = names;
	}
	return portable;
}

bool writeFile(const std::string &path, const std::string &text)
{
	QFile file(QString::fromStdString(path));
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	const QByteArray bytes = QByteArray::fromStdString(text);
	return file.write(bytes) == bytes.size();
}

bool readFile(const std::string &path, std::string &text)
{
	QFile file(QString::fromStdString(path));
	if (!file.open(QIODevice::ReadOnly))
		return false;
	text = file.readAll().toStdString();
	return true;
}

void apply(App &app, const Config &portable, std::string &summary)
{
	// Sources and scenes of THIS scene collection, by name.
	const std::vector<OverlayItem> items = readAllItems();
	const Snapshot snapshot = readSnapshot();

	int tagged = 0;
	int skipped = 0;
	app.modify([&](Config &config) {
		const bool locked = config.settings.locked; // a profile never locks or unlocks the panel
		config.settings = portable.settings;
		config.settings.locked = locked;
		for (const TagAssignment &tag : portable.tags) {
			const auto found = std::find_if(items.begin(), items.end(), [&](const OverlayItem &item) {
				return item.sourceName == tag.sourceName;
			});
			if (found == items.end()) {
				++skipped;
				continue;
			}
			setTag(config, found->sourceUuid, found->sourceName, tag.tag);
			++tagged;
		}

		config.rules.clear();
		for (Rule rule : portable.rules) {
			std::vector<std::string> uuids;
			for (const std::string &sceneName : rule.scenes) {
				const auto scene =
					std::find_if(snapshot.scenes.begin(), snapshot.scenes.end(),
						     [&](const SceneInfo &s) { return s.name == sceneName; });
				if (scene != snapshot.scenes.end())
					uuids.push_back(scene->uuid);
			}
			rule.scenes = uuids;
			if (rule.scope == Scope::ChosenScenes && rule.scenes.empty())
				rule.scope = Scope::SameScene; // none of its scenes exist here
			config.rules.push_back(rule);
		}
		config.rulesInitialised = true;
	});

	summary = std::to_string(tagged) + (tagged == 1 ? " label" : " labels") + " applied";
	if (skipped > 0)
		summary += ", " + std::to_string(skipped) + " skipped (no source with that name here)";
}

} // namespace

std::vector<std::string> listProfiles()
{
	std::vector<std::string> names;
	const std::string folder = profileFolder();
	if (folder.empty())
		return names;
	const QStringList files = QDir(QString::fromStdString(folder)).entryList({"*.json"}, QDir::Files, QDir::Name);
	for (const QString &file : files)
		names.push_back(file.left(file.size() - 5).toStdString());
	std::sort(names.begin(), names.end());
	return names;
}

bool saveProfile(const App &app, const std::string &name)
{
	const std::string path = profilePath(name);
	if (path.empty())
		return false;
	return writeFile(path, configToJson(makePortable(app.config())));
}

bool loadProfile(App &app, const std::string &name, std::string &summary)
{
	const std::string path = profilePath(name);
	std::string text;
	Config portable;
	if (path.empty() || !readFile(path, text) || !configFromJson(text, portable)) {
		summary = "That profile couldn't be read.";
		return false;
	}
	apply(app, portable, summary);
	return true;
}

bool deleteProfile(const std::string &name)
{
	const std::string path = profilePath(name);
	return !path.empty() && QFile::remove(QString::fromStdString(path));
}

bool exportSetup(const App &app, const std::string &path)
{
	return writeFile(path, configToJson(makePortable(app.config())));
}

bool importSetup(App &app, const std::string &path, std::string &summary)
{
	std::string text;
	Config portable;
	if (!readFile(path, text) || !configFromJson(text, portable)) {
		summary = "That file isn't a Laycue setup.";
		return false;
	}
	apply(app, portable, summary);
	return true;
}

} // namespace laycue
