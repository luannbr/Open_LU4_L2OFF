#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace runtime {

// Portable OS boundary for the machine-bound validation performed by current
// CacheD 0x00459928 and L2Server 0x00804ce4. All nine helper bodies have
// identical sizes and normalized/semantic/mnemonic fingerprints in both
// pinned images. Only linked addresses differ. Their original names are
// absent, so this macro-neutral transport has one physical Shared owner.
struct SerialKeyHostIdentity {
    std::string computerName;
    std::vector<std::array<std::uint8_t, 6>> adapterAddresses;
};

SerialKeyHostIdentity ReadSerialKeyHostIdentity();

bool ValidateSerialKey(const std::string& serialKey, const SerialKeyHostIdentity& identity);

// Deployment-tooling boundary. Recovered product code never calls this
// function. It exists so an operator can create a host-bound SerialKey.txt
// without weakening or bypassing the current validation path. One key is
// returned for each of the target-visible adapters, in validation order.
std::vector<std::string> GenerateSerialKeys(const SerialKeyHostIdentity& identity);

} // namespace runtime
