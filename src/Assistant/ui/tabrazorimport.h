// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string>

namespace xuoassist {

struct RazorImportResult;

bool PublishRazorProfile(const std::string &name, const RazorImportResult &preview);
void TabRazorImport_Draw();
void TabRazorImport_Reset();

} // namespace xuoassist
