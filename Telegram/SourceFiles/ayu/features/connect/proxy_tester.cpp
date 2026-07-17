// This file is part of WeGram,
// a fork of AyuGram with "works without VPN" features.
// Based on AyuGramDesktop (Copyright @Radolyn, 2026).
#include "ayu/features/connect/proxy_tester.h"

#include <QtCore/QTimer>

namespace WeGram::Connect {
namespace {

constexpr auto kTimeoutMs = 5000;

// Telegram DC #2 endpoint used as the "canary": if a proxy can open
// a tunnel to this address, it is useful for Telegram traffic.
constexpr auto kCanaryHost = "149.154.167.51";
constexpr auto kCanaryPort = 443;

} // namespace

ProxyTester::ProxyTester(MTP::ProxyData proxy, QObject *parent)
: QObject(parent)
, _proxy(std::move(proxy)) {
	_result.proxy = _proxy;
}

ProxyTestResult ProxyTester::result() const {
	return _result;
}

void ProxyTester::start() {
	_socket = new QTcpSocket(this);
	connect(
		_socket, &QTcpSocket::connected,
		this, [this] {
			_result.reachable = true;
			if (_proxy.type == MTP::ProxyData::Type::Socks5) {
				runSocks5Handshake();
			} else {
				runMtprotoPing();
			}
		});
	connect(
		_socket, &QTcpSocket::errorOccurred,
		this, [this](QAbstractSocket::SocketError) { fail(); });
	connect(
		_socket, &QTcpSocket::readyRead,
		this, [this] {
			if (_proxy.type != MTP::ProxyData::Type::Socks5) {
				return;
			}
			const auto reply = _socket->readAll();
			if (_stage == 1 && reply.size() >= 2
				&& quint8(reply[0]) == 0x05 && quint8(reply[1]) == 0x00) {
				// Greeting accepted, no auth. Ask to CONNECT to the canary.
				_stage = 2;
				QByteArray req("\x05\x01\x00\x01", 4);
				const auto parts = QString::fromLatin1(kCanaryHost).split(u'.');
				for (const auto &p : parts) req.append(char(p.toUInt()));
				req.append(char(kCanaryPort >> 8));
				req.append(char(kCanaryPort & 0xff));
				_socket->write(req);
			} else if (_stage == 2 && reply.size() >= 2
				&& quint8(reply[0]) == 0x05 && quint8(reply[1]) == 0x00) {
				// Tunnel to Telegram DC established through the proxy.
				succeed(true);
			} else {
				fail();
			}
		});

	_timer.start();

	// Overall timeout.
	QTimer::singleShot(kTimeoutMs, this, [this] { fail(); });

	_socket->connectToHost(_proxy.host, quint16(_proxy.port));
}

void ProxyTester::runSocks5Handshake() {
	_stage = 1;
	// VER=5, 1 auth method, NO AUTH (0x00).
	_socket->write(QByteArray("\x05\x01\x00", 3));
}

void ProxyTester::runMtprotoPing() {
	// TODO: proper MTProto obfuscated2 handshake.
	// For now a reachable MTProxy port is a good enough signal —
	// the client itself will fully verify it after applying.
	succeed(false);
}

void ProxyTester::succeed(bool deliversTelegram) {
	if (_result.latencyMs >= 0) {
		return; // already finished
	}
	_result.deliversTelegram = deliversTelegram;
	_result.latencyMs = int(_timer.elapsed());
	if (onFinished) {
		onFinished();
	}
}

void ProxyTester::fail() {
	if (_result.latencyMs >= 0) {
		return; // already finished
	}
	_result.deliversTelegram = false;
	_result.latencyMs = -1;
	if (_result.reachable) {
		// TCP worked but handshake did not: mark latency as "very bad"
		// so sorting paddles it down, but we keep `reachable` info.
		_result.latencyMs = -1;
	}
	if (onFinished) {
		onFinished();
	}
}

} // namespace WeGram::Connect
