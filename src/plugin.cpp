// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Chris DuPont / Xtreme Sounds

#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
    pluginInstance = p;
    p->addModel(modelS8);
    p->addModel(modelFireHost);
}
