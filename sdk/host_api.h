/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * perINKpheral host API: the contract between the host firmware and wasm
 * applications. Applications include this header and link the imports.
 * The host builds its import table from the same names.
 *
 * The API is not frozen before the first release and is not versioned
 * until then. A change requires rebuilding all applications.
 * HOST_API_VERSION and the "abi" field of a manifest stay at 2 until the
 * first release.
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
