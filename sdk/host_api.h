/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * perINKpheral host API: the contract between the host firmware and wasm
 * applications. Applications include this header and link the imports.
 * The host builds its import table from the same names.
 *
 * ABI 3 was frozen in firmware 0.1.30. Existing names, signatures,
 * privileges, layouts, values and meanings remain available on later
 * firmware. Changing a signature or meaning requires a new ABI; additions
 * after the 0.1.30 floor use HOST_SINCE_<name> and a firmware minimum.
 * ABI 2 is unsupported.
 * Commands use subject then verb (gfx_draw_text); queries name what is
 * asked (gfx_width); host-called events are app_on_<what happened>.
 *
 * The contract is written by subject in host/, and this header includes all
 * of it: an application includes this one. The tools read the headers in
 * the order they are included here.
 */
#pragma once

#include "host/base.h"
#include "host/app.h"
#include "host/gfx.h"
#include "host/ui.h"
#include "host/net.h"
#include "host/store.h"
#include "host/time.h"
#include "host/sys.h"
