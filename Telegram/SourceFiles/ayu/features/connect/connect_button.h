// This file is part of WeGram,
// a fork of AyuGram with "works without VPN" features.
// Based on AyuGramDesktop (Copyright @Radolyn, 2026).
//
// UI glue between the Proxies settings box and the Connect autopilot.
// Kept in our own module so the upstream connection_box.cpp gets
// a one-line, merge-friendly patch.
#pragma once

namespace Ui {
class BoxContent;
} // namespace Ui

namespace WeGram::Connect {

// Adds the "Connect me" button to the proxies box and wires
// it to the Controller (button text follows the autopilot state,
// result is reported with toasts).
void InjectIntoProxiesBox(Ui::BoxContent *box);

} // namespace WeGram::Connect
