// This file is part of WeGram,
// a fork of AyuGram with "works without VPN" features.
// Based on AyuGramDesktop (Copyright @Radolyn, 2026).
#pragma once

#include "mtproto/mtproto_proxy_data.h"

#include <vector>
#include <QString>

namespace WeGram::Connect {

// One remote list of proxies (e.g. a GitHub raw file or a JSON API).
struct ProxySource {
	QString name;   // human readable, for logs/UI
	QString url;    // where to GET the list
	bool enabled = true;
};

// Built-in default sources (checked alive on 2026-07-17).
// Every source returns plain text with "ip:port" lines (SOCKS5).
std::vector<ProxySource> DefaultSources();

// Result of parsing a downloaded list body.
struct ParsedProxies {
	std::vector<MTP::ProxyData> socks5;   // plain "ip:port" lines
	std::vector<MTP::ProxyData> mtproto;  // tg://proxy links inside the text
};

// Parses a downloaded list body.
// Supports two formats mixed in one body:
//   1) ip:port per line            -> SOCKS5 proxy
//   2) tg://proxy?server=..&port=..&secret=.. -> MTProto proxy
ParsedProxies ParseProxyList(const QByteArray &body);

} // namespace WeGram::Connect
