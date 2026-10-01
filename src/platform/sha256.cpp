#include "platform/host_platform.h"

#include "thirdparty/crypto/sha256.h"

namespace pinyon_shift::platform {

std::array<uint8_t, 32> Sha256(std::span<const std::byte> bytes) {
  sha256::SHA256 hash;
  hash.add(bytes.data(), bytes.size());
  std::array<uint8_t, 32> digest{};
  hash.getHash(digest.data());
  return digest;
}

}  // namespace pinyon_shift::platform
