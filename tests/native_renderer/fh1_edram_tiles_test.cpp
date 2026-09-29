#include <cstdio>

#include <rex/graphics/fh1_edram_tiles.h>

using rex::graphics::Fh1EdramTiles;

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,   \
                   #condition);                                                 \
      ++failures;                                                               \
    }                                                                           \
  } while (false)

constexpr uint32_t kA = 0x100, kB = 0x200;

void TestClaimReturnsPreviousOwnersRuns() {
  Fh1EdramTiles tiles;
  tiles.Reset();
  CHECK(tiles.Owner(0) == Fh1EdramTiles::kNoOwner);
  // Unowned tiles have nothing to transfer.
  CHECK(tiles.Claim(0, 10, kA, true).empty());
  CHECK(tiles.Owner(9) == kA);
  // Repeating a claim of the same range is free and changes nothing.
  const uint64_t generation = tiles.generation();
  CHECK(tiles.Claim(0, 10, kA, true).empty());
  CHECK(tiles.generation() == generation);
  // Taking part of A's range returns one run of A's tiles.
  const auto runs = tiles.Claim(4, 10, kB, true);
  CHECK(runs.size() == 1);
  CHECK(runs.size() == 1 && runs[0].first == 4 && runs[0].count == 6 &&
        runs[0].previous_owner == kA);
  CHECK(tiles.Owner(3) == kA && tiles.Owner(4) == kB && tiles.Owner(13) == kB);
  CHECK(tiles.generation() > generation);
  // A's cached claim was dropped, so claiming its range again transfers.
  CHECK(tiles.Claim(0, 10, kA, true).size() == 1);
}

void TestClaimWithoutTransferAndWrapping() {
  Fh1EdramTiles tiles;
  tiles.Reset();
  tiles.Claim(0, 8, kA, true);
  // A resolve clear takes the tiles without their contents.
  CHECK(tiles.Claim(0, 8, kB, false).empty());
  CHECK(tiles.Owner(7) == kB);
  // Claims wrap at the end of EDRAM.
  const uint32_t last = rex::graphics::xenos::kEdramTileCount - 2;
  tiles.Claim(last, 4, kA, true);
  CHECK(tiles.Owner(last) == kA && tiles.Owner(1) == kA);
}

void TestRectClaims() {
  Fh1EdramTiles tiles;
  tiles.Reset();
  // A 4x2 rectangle at pitch 10 owned by A alone moves in one piece.
  tiles.Claim(0, 20, kA, true);
  auto claim = tiles.ClaimRect(0, 10, kB, 1, 0, 5, 2);
  CHECK(!claim.per_row && claim.previous_owner == kA);
  CHECK(tiles.Owner(1) == kB && tiles.Owner(14) == kB && tiles.Owner(15) == kA);
  // Mixed previous owners fall back to row claims, leaving the tiles alone.
  claim = tiles.ClaimRect(0, 10, kA, 0, 0, 6, 1);
  CHECK(claim.per_row);
  CHECK(tiles.Owner(1) == kB);
  // An empty rectangle claims nothing.
  claim = tiles.ClaimRect(0, 10, kA, 3, 0, 3, 2);
  CHECK(!claim.per_row && claim.previous_owner == Fh1EdramTiles::kNoOwner);
}

void TestStencil() {
  Fh1EdramTiles tiles;
  tiles.Reset();
  CHECK(tiles.AnyStencil(0, 4));
  tiles.MarkStencil(0, 4, false);
  CHECK(!tiles.AnyStencil(0, 4) && tiles.AnyStencil(0, 5));
  CHECK(!tiles.StencilNonzero(3) && tiles.StencilNonzero(4));
}

}  // namespace

int main() {
  TestClaimReturnsPreviousOwnersRuns();
  TestClaimWithoutTransferAndWrapping();
  TestRectClaims();
  TestStencil();
  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("fh1_edram_tiles tests passed\n");
  return 0;
}
