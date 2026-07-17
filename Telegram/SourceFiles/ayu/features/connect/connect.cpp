// This file is part of WeGram,
// a fork of AyuGram with "works without VPN" features.
// Based on AyuGramDesktop (Copyright @Radolyn, 2026).
#include "ayu/features/connect/connect.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_settings_proxy.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSet>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>
#include <range/v3/algorithm/any_of.hpp>

namespace WeGram::Connect {
namespace {

constexpr auto kFetchTimeoutMs = 15000;
constexpr auto kMaxCandidatesPerRace = 40; // don't hammer the network
constexpr auto kMaxParallelTests = 16;

// Local settings file sits next to other AyuGram data.
[[nodiscard]] QString SettingsPath() {
	return cWorkingDir() + u"tdata/wegram_connect.json"_q;
}

} // namespace

Controller::Controller()
: _net(std::make_unique<QNetworkAccessManager>()) {
}

State Controller::state() const {
	return _state;
}

const ConnectSettings &Controller::settings() const {
	return _settings;
}

rpl::producer<State> Controller::stateChanges() const {
	return _stateStream.events();
}

void Controller::setState(State state) {
	if (_state != state) {
		_state = state;
		_stateStream.fire(state);
	}
}

void Controller::loadSettings() {
	auto file = QFile(SettingsPath());
	if (!file.open(QIODevice::ReadOnly)) {
		_settings.sources = DefaultSources();
		return;
	}
	const auto doc = QJsonDocument::fromJson(file.readAll());
	const auto obj = doc.object();
	_settings.autoConnectOnStart = obj.value(u"autoConnectOnStart"_q).toBool();
	_settings.autoSwitchWhenDegraded
		= obj.value(u"autoSwitchWhenDegraded"_q).toBool(true);
	_settings.degradedKbps
		= obj.value(u"degradedKbps"_q).toInt(100);
	// TODO: restore custom sources from "sources" array.
	if (_settings.sources.empty()) {
		_settings.sources = DefaultSources();
	}
}

void Controller::saveSettings() const {
	auto obj = QJsonObject();
	obj.insert(u"autoConnectOnStart"_q, _settings.autoConnectOnStart);
	obj.insert(u"autoSwitchWhenDegraded"_q, _settings.autoSwitchWhenDegraded);
	obj.insert(u"degradedKbps"_q, _settings.degradedKbps);
	// Atomic-ish save: temp file + rename, so a killed process
	// never leaves a torn JSON behind.
	auto tmp = QFile(SettingsPath() + u".tmp"_q);
	if (tmp.open(QIODevice::WriteOnly)) {
		tmp.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
		tmp.close();
		QFile::rename(SettingsPath() + u".tmp"_q, SettingsPath());
	}
}

void Controller::connectToBest() {
	if (_state == State::Fetching || _state == State::Testing) {
		return; // a race is already on its way
	}
	setState(State::Fetching);

	auto collected = std::make_shared<std::vector<MTP::ProxyData>>();
	auto remaining = std::make_shared<int>(0);

	for (const auto &source : _settings.sources) {
		if (!source.enabled) {
			continue;
		}
		++(*remaining);
		auto request = QNetworkRequest(QUrl(source.url));
		request.setTransferTimeout(kFetchTimeoutMs);
		auto *reply = _net->get(request);
		QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
			reply->deleteLater();
			if (reply->error() == QNetworkReply::NoError) {
				auto parsed = ParseProxyList(reply->readAll());
				for (auto &p : parsed.socks5) collected->push_back(std::move(p));
				for (auto &p : parsed.mtproto) collected->push_back(std::move(p));
			} else {
				DEBUG_LOG(("[WeGram] source %1 failed: %2")
					.arg(source.name, reply->errorString()));
			}
			if (--(*remaining) == 0) {
				onSourcesFetched(std::move(*collected));
			}
		});
	}
	if (*remaining == 0) {
		setState(State::Failed);
	}
}

void Controller::onSourcesFetched(std::vector<MTP::ProxyData> candidates) {
	// Dedupe by host:port.
	auto seen = QSet<QString>();
	auto unique = std::vector<MTP::ProxyData>();
	for (auto &p : candidates) {
		const auto key = p.host + u':' + QString::number(p.port);
		if (!seen.contains(key)) {
			seen.insert(key);
			unique.push_back(std::move(p));
		}
	}
	if (unique.empty()) {
		DEBUG_LOG(("[WeGram] no candidates fetched"));
		setState(State::Failed);
		return;
	}
	if (unique.size() > kMaxCandidatesPerRace) {
		unique.resize(kMaxCandidatesPerRace);
	}
	DEBUG_LOG(("[WeGram] racing %1 candidates").arg(unique.size()));

	setState(State::Testing);
	_testers.clear();
	_testers.reserve(unique.size());

	auto results = std::make_shared<std::vector<ProxyTestResult>>();
	auto remaining = std::make_shared<int>(int(unique.size()));

	// TODO: chunk into waves of kMaxParallelTests instead of all at once.
	for (auto &p : unique) {
		auto tester = std::make_unique<ProxyTester>(std::move(p));
		const auto raw = tester.get();
		raw->onFinished = [=] {
			results->push_back(raw->result());
			if (int(results->size()) == *remaining) {
				onRaceFinished(std::move(*results));
			}
		};
		_testers.push_back(std::move(tester));
		raw->start();
	}
}

void Controller::onRaceFinished(std::vector<ProxyTestResult> results) {
	// Winners first: proxies that proved they deliver Telegram,
	// then simply reachable ones; each group sorted by latency.
	std::sort(results.begin(), results.end(), [](const auto &a, const auto &b) {
		if (a.deliversTelegram != b.deliversTelegram) {
			return a.deliversTelegram; // true first
		}
		const auto al = (a.latencyMs < 0) ? 1'000'000 : a.latencyMs;
		const auto bl = (b.latencyMs < 0) ? 1'000'000 : b.latencyMs;
		return al < bl;
	});

	for (const auto &candidate : results) {
		if (candidate.deliversTelegram && applyWinner(candidate)) {
			DEBUG_LOG(("[WeGram] winner %1:%2, %3 ms")
				.arg(candidate.proxy.host)
				.arg(candidate.proxy.port)
				.arg(candidate.latencyMs));
			_testers.clear();
			setState(State::Connected);
			return;
		}
	}
	_testers.clear();
	DEBUG_LOG(("[WeGram] no proxy delivered Telegram"));
	setState(State::Failed);
}

bool Controller::applyWinner(const ProxyTestResult &winner) {
	if (!Core::IsAppLaunched()) {
		return false;
	}
	auto &proxy = Core::App().settings().proxy();
	// Remember the winner in the known list (if new), then select it.
	const auto key = winner.proxy.host + u':' + QString::number(winner.proxy.port);
	const auto exists = ranges::any_of(proxy.list(), [&](const MTP::ProxyData &p) {
		return p.host == winner.proxy.host && p.port == winner.proxy.port;
	});
	if (!exists) {
		proxy.list().push_back(winner.proxy);
	}
	proxy.setSelected(winner.proxy);
	proxy.setSettings(MTP::ProxyData::Settings::Enabled);
	Core::App().saveSettings();
	return true;
}

void Controller::recheckCurrentConnection() {
	// Level-1 stub: simply re-run the race.
	// Level-3 will compare the measured speed against degradedKbps first.
	connectToBest();
}

Controller &controller() {
	static auto instance = Controller();
	return instance;
}

} // namespace WeGram::Connect
