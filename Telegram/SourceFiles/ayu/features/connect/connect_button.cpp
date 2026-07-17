// This file is part of WeGram,
// a fork of AyuGram with "works without VPN" features.
// Based on AyuGramDesktop (Copyright @Radolyn, 2026).
#include "ayu/features/connect/connect_button.h"

#include "ayu/features/connect/connect.h"
#include "ui/layers/box_content.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"

namespace WeGram::Connect {
namespace {

// TODO(wegram): move to tr::ayu_wegram_* langpack keys when adding
// proper localization for the fork.
[[nodiscard]] QString ButtonText(State state) {
	switch (state) {
	case State::Idle:
	case State::Failed:
		return u"\U0001F6F0 WeGram: Connect me"_q;
	case State::Fetching:
		return u"WeGram: searching proxies..."_q;
	case State::Testing:
		return u"WeGram: racing candidates..."_q;
	case State::Applying:
		return u"WeGram: applying..."_q;
	case State::Connected:
		return u"\u2705 WeGram: connected"_q;
	}
	Unexpected("State in WeGram::Connect::ButtonText.");
}

[[nodiscard]] QString ToastText(State state) {
	switch (state) {
	case State::Connected:
		return u"WeGram connected you to the fastest working proxy. No VPN needed!"_q;
	case State::Failed:
		return u"WeGram could not find a working proxy right now. Try again in a minute."_q;
	default:
		return {};
	}
}

} // namespace

void InjectIntoProxiesBox(Ui::BoxContent *box) {
	auto button = box->addButton(
		rpl::single(ButtonText(controller().state())),
		[] { controller().connectToBest(); });

	const auto raw = button.data();
	if (controller().state() == State::Fetching
		|| controller().state() == State::Testing) {
		raw->setEnabled(false);
	}

	controller().stateChanges(
	) | rpl::start_with_next([=](State state) {
		raw->setText(rpl::single(ButtonText(state)));
		const auto busy = (state == State::Fetching)
			|| (state == State::Testing)
			|| (state == State::Applying);
		raw->setEnabled(!busy);
		if (const auto toast = ToastText(state); !toast.isEmpty()) {
			Ui::Toast::Show(toast);
		}
	}, box->lifetime());
}

} // namespace WeGram::Connect
