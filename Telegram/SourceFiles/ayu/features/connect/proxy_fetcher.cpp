// This file is part of WeGram,
// a fork of AyuGram with "works without VPN" features.
// Based on AyuGramDesktop (Copyright @Radolyn, 2026).
#include "ayu/features/connect/proxy_fetcher.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace WeGram::Connect {

std::vector<ProxySource> DefaultSources() {
	return {
		{
			u"ProxyScrape (SOCKS5)"_q,
			u"https://api.proxyscrape.com/v4/free-proxy-list/get?request=display_proxies&protocol=socks5&proxy_format=ipport&format=text"_q,
		},
		{
			u"TheSpeedX (SOCKS5)"_q,
			u"https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/socks5.txt"_q,
		},
		{
			u"hookzof (SOCKS5)"_q,
			u"https://raw.githubusercontent.com/hookzof/socks5_list/master/proxy.txt"_q,
		},
	};
}

namespace {

[[nodiscard]] MTP::ProxyData MakeSocks5(const QString &host, quint16 port) {
	auto result = MTP::ProxyData();
	result.type = MTP::ProxyData::Type::Socks5;
	result.host = host;
	result.port = port;
	return result;
}

[[nodiscard]] MTP::ProxyData ProxyFromLink(const QString &link) {
	// tg://proxy?server=1.2.3.4&port=443&secret=ee...
	const auto query = QUrlQuery(QUrl(link));
	const auto host = query.queryItemValue(u"server"_q);
	const auto port = query.queryItemValue(u"port"_q).toUInt();
	const auto secret = query.queryItemValue(u"secret"_q);
	if (host.isEmpty() || !port) {
		return {};
	}
	auto result = MTP::ProxyData();
	result.type = secret.isEmpty()
		? MTP::ProxyData::Type::Socks5
		: MTP::ProxyData::Type::Mtproto;
	result.host = host;
	result.port = port;
	// secret is hex; ee-prefixed = FakeTLS, dd-prefixed = randomized padding
	const auto raw = QByteArray::fromHex(secret.toLatin1());
	result.secret = bytes::vector(
		reinterpret_cast<const bytes::type*>(raw.constData()),
		reinterpret_cast<const bytes::type*>(raw.constData()) + raw.size());
	return result;
}

} // namespace

ParsedProxies ParseProxyList(const QByteArray &body) {
	auto result = ParsedProxies();

	// tg://proxy links may appear anywhere in the text.
	static const auto linkRe = QRegularExpression(
		u"tg://proxy\\?[^\\s\"'<>]+"_q);
	auto it = linkRe.globalMatch(QString::fromUtf8(body));
	while (it.hasNext()) {
		if (auto proxy = ProxyFromLink(it.next().captured());
			proxy.type != MTP::ProxyData::Type::None) {
			result.mtproto.push_back(std::move(proxy));
		}
	}

	// Plain "ip:port" lines -> SOCKS5.
	static const auto lineRe = QRegularExpression(
		u"^(\\d{1,3}(?:\\.\\d{1,3}){3}):(\\d{2,5})\\s*$"_q);
	const auto lines = QString::fromUtf8(body).split(u'\n');
	for (const auto &line : lines) {
		const auto match = lineRe.match(line.trimmed());
		if (!match.hasMatch()) {
			continue;
		}
		const auto host = match.captured(1);
		const auto port = match.captured(2).toUShort();
		if (port) {
			result.socks5.push_back(MakeSocks5(host, port));
		}
	}
	return result;
}

} // namespace WeGram::Connect
