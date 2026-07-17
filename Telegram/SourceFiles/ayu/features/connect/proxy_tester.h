// This file is part of WeGram,
// a fork of AyuGram with "works without VPN" features.
// Based on AyuGramDesktop (Copyright @Radolyn, 2026).
#pragma once

#include "mtproto/mtproto_proxy_data.h"

#include <QtNetwork/QTcpSocket>
#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>

namespace WeGram::Connect {

// Verdict about a single proxy, produced by ProxyTester.
struct ProxyTestResult {
	MTP::ProxyData proxy;
	bool reachable = false;      // TCP connect succeeded
	bool deliversTelegram = false; // handshake to a Telegram DC succeeded
	int latencyMs = -1;          // total handshake time, -1 if failed
};

// Tests one proxy. The most important test:
// for SOCKS5 we open a tunnel THROUGH the proxy to a real Telegram DC
// and check the proxy's answer. This filters out proxies that are alive
// but useless (blocked by the censor, honeypots, etc.).
//
// Usage: create, call start(), wait for finished(), then read result().
class ProxyTester final : public QObject {
public:
	explicit ProxyTester(MTP::ProxyData proxy, QObject *parent = nullptr);
	~ProxyTester() override;

	void start();

	[[nodiscard]] ProxyTestResult result() const;

	void finished(); // emitted once; QObject signal-like via lambda callback
	Fn<void()> onFinished;

private:
	void runSocks5Handshake();
	void runMtprotoPing();
	void fail();
	void succeed(bool deliversTelegram);

	MTP::ProxyData _proxy;
	ProxyTestResult _result;
	QTcpSocket *_socket = nullptr;
	QElapsedTimer _timer;
	int _stage = 0;

};

} // namespace WeGram::Connect
