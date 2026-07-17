// This file is part of WeGram,
// a fork of AyuGram with "works without VPN" features.
// Based on AyuGramDesktop (Copyright @Radolyn, 2026).
//
// "Connect" is the autopilot: it fetches fresh proxies from public
// sources, races them against a live Telegram DC, picks the winner
// and applies it to the app settings — one button, no VPN.
#pragma once

#include "ayu/features/connect/proxy_fetcher.h"
#include "ayu/features/connect/proxy_tester.h"

#include <memory>
#include <vector>

class QNetworkAccessManager;

namespace WeGram::Connect {

// High-level state, observed by UI (banner, settings badge).
enum class State {
	Idle,        // nothing is happening
	Fetching,    // downloading proxy lists
	Testing,     // racing candidates
	Applying,    // writing the winner into settings
	Connected,   // hooray, we have a working proxy
	Failed,      // no usable proxy found
};

// Settings for the Connect feature (stored separately from the
// 113-field AyuSettings monolith, in tdata/wegram_connect.json).
struct ConnectSettings {
	bool autoConnectOnStart = false; // engage autopilot at app start
	bool autoSwitchWhenDegraded = true; // re-race when connection gets slow
	int degradedKbps = 100;            // below this speed we re-race
	std::vector<ProxySource> sources; // may be extended by remote config
};

class Controller final {
public:
	Controller();

	[[nodiscard]] State state() const;
	[[nodiscard]] const ConnectSettings &settings() const;

	// Called when state changes (UI subscribes here).
	[[nodiscard]] rpl::producer<State> stateChanges() const;

	// The big button. Starts the whole pipeline:
	// fetch -> dedupe -> race (top-N by latency) -> apply best.
	void connectToBest();

	// Re-race if the current connection is slower than threshold.
	// Stub for Level-1; the speedometer wiring comes in Level-3.
	void recheckCurrentConnection();

	// Persists/loads ConnectSettings (tdata/wegram_connect.json).
	void loadSettings();
	void saveSettings() const;

private:
	void onSourcesFetched(std::vector<MTP::ProxyData> candidates);
	void onRaceFinished(std::vector<ProxyTestResult> results);
	bool applyWinner(const ProxyTestResult &winner);
	void setState(State state);

	ConnectSettings _settings;
	State _state = State::Idle;
	std::unique_ptr<QNetworkAccessManager> _net;
	std::vector<std::unique_ptr<ProxyTester>> _testers;
	rpl::event_stream<State> _stateStream;

};

// Global access point (like AyuSettings::getInstance()).
Controller &controller();

} // namespace WeGram::Connect
