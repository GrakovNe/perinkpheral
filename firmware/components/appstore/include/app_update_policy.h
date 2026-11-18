/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

bool app_update_version_is_newer(const char *installed, const char *candidate);

/* Automatic updates only. Both manifests must parse on this host and keep
 * their identity. Versions must be stable X.Y.Z releases, strictly increasing.
 * New privileges or network destinations require a manual installation.
 * Certificate names may only be removed; the installer must also compare
 * the remaining certificate files byte for byte before committing. */
bool app_update_policy_allows(const char *installed_json, const char *candidate_json,
                              const char *candidate_version, const char *firmware_version);
