// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "orbit/native_provider.hpp"
#include <cstdint>

namespace hui::ps4 {
// One native-owned dialog. All calls run on the application's frame thread.
class ProviderDialog final : public orbit::ProviderWindow {
public:
    // Cheap: nothing is loaded until the first open. Setting up the dialog
    // starts a ~126 MB system process; on PS4 the service has Orbit Store
    // Browser open provider pages instead, so this window is normally unused.
    bool initialize() { return true; }
    bool open(const std::string &url) override;
    int status() override;
    int close() override;
    int error_code() const override { return error_code_; }
private:
    bool prepare();
    bool ready_ = false, owned_ = false, running_ = false, requested_close_ = false;
    std::int64_t opened_at_ = 0;
    int last_status_ = -1;
    int error_code_ = 0;
};
} // namespace hui::ps4
