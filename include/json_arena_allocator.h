// Fixed-capacity bump allocator for ArduinoJson's JsonDocument, backed by a
// caller-provided buffer (a local/stack array, in practice) instead of the
// heap. See docs/firmware-architecture.md's "Memory" section for why: the
// esp-aes TLS decrypt path needs a small scratch allocation from the same
// internal/DMA-capable heap our own JsonDocuments were competing for
// (confirmed once on real hardware as an `esp-aes: Failed to allocate
// memory for start alignment buffer` failure — see docs/hardware.md).
// Moving our own parsing off that heap removes us from that contention
// entirely, rather than just reducing it.
//
// Not a general-purpose allocator — deliberately narrow, matching how
// ArduinoJson actually calls it during a parse (confirmed by reading
// ArduinoJson/Memory/{MemoryPool,StringNode}.hpp in the vendored source):
// variant-node pools are allocated once and never resized, while strings
// are grown via repeated reallocate() calls as the parser streams in more
// characters. So:
//   - allocate()/reallocate() only ever need to grow the *most recent*
//     allocation in place (the common case) — supported via a bump
//     pointer plus a small per-block size header so reallocate() knows
//     how many bytes of the old block to preserve.
//   - reallocate()ing something other than the most recent block (rare —
//     ArduinoJson only does this if a document holds multiple strings
//     that grow out of order) falls back to allocate-new + copy + leak
//     the old block's space within the arena.
//   - deallocate() only reclaims space if it's freeing the most recent
//     block; otherwise it's a no-op leak within the arena.
// Both "leak within the arena" cases are fine here: the arena's lifetime
// is one parse, and the whole buffer is reclaimed at once when it goes out
// of scope (or via reset(), for a reused instance) — nothing is leaked
// across parses, only (at worst) under-used within a single one.
#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

class JsonArenaAllocator : public ArduinoJson::Allocator {
 public:
  JsonArenaAllocator(uint8_t *buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {}

  void *allocate(size_t size) override {
    return allocBlock(size);
  }

  void deallocate(void *ptr) override {
    if (!ptr) {
      return;
    }
    const size_t blockOffset = offsetOfBlock(ptr);
    if (blockOffset == lastBlockOffset_) {
      offset_ = lastBlockOffset_;  // was the most recent block: reclaim it
    }
    // else: leaked within the arena until this allocator goes out of scope.
  }

  void *reallocate(void *ptr, size_t newSize) override {
    if (!ptr) {
      return allocBlock(newSize);
    }

    const size_t blockOffset = offsetOfBlock(ptr);
    BlockHeader *header = headerAt(blockOffset);

    if (blockOffset == lastBlockOffset_) {
      // Most recent block: try to grow/shrink in place.
      const size_t newTotal = align(sizeof(BlockHeader) + newSize);
      if (blockOffset + newTotal > capacity_) {
        overflowed_ = true;
        return nullptr;
      }
      header->size = newSize;
      offset_ = blockOffset + newTotal;
      return ptr;
    }

    // Not the most recent block: allocate a fresh one and copy the old
    // data over (only as many bytes as the old block actually had).
    void *newPtr = allocBlock(newSize);
    if (newPtr) {
      const size_t toCopy = header->size < newSize ? header->size : newSize;
      memcpy(newPtr, ptr, toCopy);
    }
    return newPtr;
  }

  // True if any allocation since construction (or the last reset()) had to
  // be refused for lack of space — mirrors JsonDocument::overflowed(), but
  // at the arena level, so a too-small buffer size shows up even if the
  // caller forgets to check the document's own overflow flag.
  bool overflowed() const { return overflowed_; }

  // Bytes handed out so far (including per-block headers) — use this to
  // size N against real hardware numbers instead of guessing.
  // JsonDocument::memoryUsage() looks like the obvious way to get this,
  // but it's deprecated in this ArduinoJson version (7.4.3) and
  // unconditionally returns 0 (confirmed in the vendored source,
  // JsonDocument.hpp) — this is the real number.
  size_t bytesUsed() const { return offset_; }

  // Reclaims the whole arena for reuse across multiple parses with one
  // instance, instead of constructing a fresh allocator (and buffer) each
  // time.
  void reset() {
    offset_ = 0;
    lastBlockOffset_ = 0;
    overflowed_ = false;
  }

 private:
  struct BlockHeader {
    size_t size;
  };

  static size_t align(size_t n) {
    constexpr size_t kAlign = alignof(std::max_align_t);
    return (n + kAlign - 1) & ~(kAlign - 1);
  }

  size_t offsetOfBlock(void *ptr) const {
    return reinterpret_cast<uint8_t *>(ptr) - sizeof(BlockHeader) - buffer_;
  }

  BlockHeader *headerAt(size_t blockOffset) {
    return reinterpret_cast<BlockHeader *>(buffer_ + blockOffset);
  }

  void *allocBlock(size_t size) {
    const size_t total = align(sizeof(BlockHeader) + size);
    if (offset_ + total > capacity_) {
      overflowed_ = true;
      return nullptr;
    }
    BlockHeader *header = headerAt(offset_);
    header->size = size;
    lastBlockOffset_ = offset_;
    offset_ += total;
    return header + 1;
  }

  uint8_t *buffer_;
  size_t capacity_;
  size_t offset_ = 0;
  size_t lastBlockOffset_ = 0;
  bool overflowed_ = false;
};

// Convenience wrapper: a JsonDocument backed by an N-byte buffer that's
// part of this object (so, in practice, wherever the caller declares one —
// currently the Arduino loop task's stack, which main.cpp's
// getArduinoLoopTaskStackSize() override sizes accordingly). Bundles the
// buffer, the arena allocator, and the document itself so call sites don't
// have to wire the three together by hand each time.
template <size_t N>
class StackJsonDocument {
 public:
  StackJsonDocument() : allocator_(buffer_, N), doc_(&allocator_) {}

  ArduinoJson::JsonDocument &doc() { return doc_; }

  // True if the arena ran out of space (checked independently of
  // JsonDocument::overflowed(), which only reflects the variant/string
  // pools' own bookkeeping) — log this after a real parse before trusting
  // any fixed N, per docs/firmware-architecture.md's sizing note.
  bool overflowed() const { return allocator_.overflowed() || doc_.overflowed(); }

  // Real bytes used out of N — see JsonArenaAllocator::bytesUsed().
  size_t bytesUsed() const { return allocator_.bytesUsed(); }

 private:
  uint8_t buffer_[N];
  JsonArenaAllocator allocator_;
  ArduinoJson::JsonDocument doc_;
};
