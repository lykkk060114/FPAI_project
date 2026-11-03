#pragma once

// Controlled PL input-loop interface.  The controller is deliberately opt-in:
// a missing/mismatched BOOT must never silently replace the proven CPU bridge.
#include <icraft-xrt/core/device.h>
#include <cstdint>
#include <stdexcept>
#include <functional>

namespace xunjing::pl_loop {

enum : std::uint32_t {
    XJ_BUSY = 1, XJ_DONE = 2, XJ_ERROR = 4, XJ_READY = 16,
    XJ_ACCEPTED = 32, XJ_START = 1, XJ_COMMIT = 8
};

struct Config {
    bool enabled = false;
    std::uint64_t csr_base = 0x400c4000;
    std::uint32_t timeout_polls = 1000000;
    std::uint32_t settle_polls = 64;
    // Must be implemented with the platform's proven ownership/cache fence.
    std::function<int(void *)> visibility_barrier;
    void *visibility_context = nullptr;
};

class Controller {
public:
    Controller(icraft::xrt::Device &device, Config cfg)
        : regs_(device.defaultRegRegion()), cfg_(cfg) {}

    bool available() const {
        return regs_.read(cfg_.csr_base) == 0x584a4901u;
    }

    // This is the only method that writes the PL input CSR page. It validates
    // the hardware ID and the 32-bit User_Ddr address before issuing START.
    bool start(std::uint32_t user_ddr_base, std::uint32_t capacity,
               std::uint32_t frame_id, std::uint32_t buffer) {
        if (!cfg_.enabled || !available()) return false;
        if (user_ddr_base == 0 || capacity < 2703360u ||
            (std::uint64_t(user_ddr_base) + capacity) > 0x100000000ULL)
            throw std::runtime_error("invalid PL input User_Ddr range");
        const auto status = regs_.read(cfg_.csr_base + 4);
        if ((status & XJ_READY) == 0 || (status & XJ_BUSY) != 0)
            throw std::runtime_error("PL input CSR is not READY");
        regs_.write(cfg_.csr_base + 0x0c, user_ddr_base);
        regs_.write(cfg_.csr_base + 0x10, capacity);
        regs_.write(cfg_.csr_base + 0x14, frame_id);
        regs_.write(cfg_.csr_base + 0x18, buffer);
        regs_.write(cfg_.csr_base + 0x1c, cfg_.timeout_polls);
        regs_.write(cfg_.csr_base + 0x20, cfg_.settle_polls);
        regs_.write(cfg_.csr_base + 0x08, XJ_START);
        return true;
    }

    bool accepted() const {
        if (!cfg_.enabled || !available()) return false;
        return (regs_.read(cfg_.csr_base + 4) & XJ_ACCEPTED) != 0;
    }

    // ACCEPTED is only a write-request handshake.  Never COMMIT without the
    // caller's verified device-visibility/ownership operation.
    bool commitAfterVisibility() {
        if (!accepted()) return false;
        if (!cfg_.visibility_barrier ||
            cfg_.visibility_barrier(cfg_.visibility_context) != 0)
            throw std::runtime_error("PL-to-NPU visibility barrier unavailable/failed");
        regs_.write(cfg_.csr_base + 0x08, XJ_COMMIT);
        return true;
    }

private:
    icraft::xrt::RegRegion regs_;
    Config cfg_;
};

} // namespace xunjing::pl_loop
