//===-- Implementation of memset and bzero --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STRING_MEMORY_UTILS_MEMSET_IMPLEMENTATIONS_H
#define LLVM_LIBC_SRC_STRING_MEMORY_UTILS_MEMSET_IMPLEMENTATIONS_H

#include "src/__support/common.h"
#include "src/__support/macros/optimization.h"
#include "src/__support/macros/properties/architectures.h"
#include "src/string/memory_utils/generic/byte_per_byte.h"
#include "src/string/memory_utils/op_aarch64.h"
#include "src/string/memory_utils/op_builtin.h"
#include "src/string/memory_utils/op_generic.h"
#include "src/string/memory_utils/op_x86.h"
#include "src/string/memory_utils/utils.h"

#include <stddef.h> // size_t

namespace __llvm_libc {

[[maybe_unused]] LIBC_INLINE static void
inline_memset_aligned_access_32bit(Ptr dst, uint8_t value, size_t count) {
  constexpr size_t kAlign = sizeof(uint32_t);
  if (count <= 2 * kAlign)
    return inline_memset_byte_per_byte(dst, value, count);
  size_t bytes_to_dst_align = distance_to_align_up<kAlign>(dst);
  inline_memset_byte_per_byte(dst, value, bytes_to_dst_align);
  size_t offset = bytes_to_dst_align;
  for (; offset < count - kAlign; offset += kAlign)
    store32_aligned<uint32_t>(generic::splat<uint32_t>(value), dst, offset);
  inline_memset_byte_per_byte(dst, value, count, offset);
}

[[maybe_unused]] LIBC_INLINE static void
inline_memset_aligned_access_64bit(Ptr dst, uint8_t value, size_t count) {
  constexpr size_t kAlign = sizeof(uint64_t);
  if (count <= 2 * kAlign)
    return inline_memset_byte_per_byte(dst, value, count);
  size_t bytes_to_dst_align = distance_to_align_up<kAlign>(dst);
  inline_memset_byte_per_byte(dst, value, bytes_to_dst_align);
  size_t offset = bytes_to_dst_align;
  for (; offset < count - kAlign; offset += kAlign)
    store64_aligned<uint64_t>(generic::splat<uint64_t>(value), dst, offset);
  inline_memset_byte_per_byte(dst, value, count, offset);
}

#if defined(LIBC_TARGET_ARCH_IS_X86)
[[maybe_unused]] LIBC_INLINE static void
inline_memset_x86(Ptr dst, uint8_t value, size_t count) {
#if defined(__AVX512F__)
  using uint128_t = generic_v128;
  using uint256_t = generic_v256;
  using uint512_t = generic_v512;
#elif defined(__AVX__)
  using uint128_t = generic_v128;
  using uint256_t = generic_v256;
  using uint512_t = cpp::array<generic_v256, 2>;
#elif defined(__SSE2__)
  using uint128_t = generic_v128;
  using uint256_t = cpp::array<generic_v128, 2>;
  using uint512_t = cpp::array<generic_v128, 4>;
#else
  using uint128_t = cpp::array<uint64_t, 2>;
  using uint256_t = cpp::array<uint64_t, 4>;
  using uint512_t = cpp::array<uint64_t, 8>;
#endif

  if (count == 0)
    return;
  if (count == 1)
    return generic::Memset<uint8_t>::block(dst, value);
  if (count == 2)
    return generic::Memset<uint16_t>::block(dst, value);
  if (count == 3)
    return generic::MemsetSequence<uint16_t, uint8_t>::block(dst, value);
  if (count <= 8)
    return generic::Memset<uint32_t>::head_tail(dst, value, count);
  if (count <= 16)
    return generic::Memset<uint64_t>::head_tail(dst, value, count);
  if (count <= 32)
    return generic::Memset<uint128_t>::head_tail(dst, value, count);
  if (count <= 64)
    return generic::Memset<uint256_t>::head_tail(dst, value, count);
  if (count <= 128)
    return generic::Memset<uint512_t>::head_tail(dst, value, count);
  // Aligned loop
  generic::Memset<uint256_t>::block(dst, value);
  align_to_next_boundary<32>(dst, count);
  return generic::Memset<uint256_t>::loop_and_tail(dst, value, count);
}
#endif // defined(LIBC_TARGET_ARCH_IS_X86)

#if defined(LIBC_TARGET_ARCH_IS_AARCH64)
[[maybe_unused]] LIBC_INLINE static void
inline_memset_aarch64(Ptr dst, uint8_t value, size_t count) {
  static_assert(aarch64::kNeon, "aarch64 supports vector types");
  using uint128_t = generic_v128;
  using uint256_t = generic_v256;
  using uint512_t = generic_v512;
  if (count == 0)
    return;
  if (count <= 3) {
    generic::Memset<uint8_t>::block(dst, value);
    if (count > 1)
      generic::Memset<uint16_t>::tail(dst, value, count);
    return;
  }
  if (count <= 8)
    return generic::Memset<uint32_t>::head_tail(dst, value, count);
  if (count <= 16)
    return generic::Memset<uint64_t>::head_tail(dst, value, count);
  if (count <= 32)
    return generic::Memset<uint128_t>::head_tail(dst, value, count);
  if (count <= (32 + 64)) {
    generic::Memset<uint256_t>::block(dst, value);
    if (count <= 64)
      return generic::Memset<uint256_t>::tail(dst, value, count);
    generic::Memset<uint256_t>::block(dst + 32, value);
    generic::Memset<uint256_t>::tail(dst, value, count);
    return;
  }
  if (count >= 448 && value == 0 && aarch64::neon::hasZva()) {
    generic::Memset<uint512_t>::block(dst, 0);
    align_to_next_boundary<64>(dst, count);
    return aarch64::neon::BzeroCacheLine::loop_and_tail(dst, 0, count);
  } else {
    generic::Memset<uint128_t>::block(dst, value);
    align_to_next_boundary<16>(dst, count);
    return generic::Memset<uint512_t>::loop_and_tail(dst, value, count);
  }
}
#endif // defined(LIBC_TARGET_ARCH_IS_AARCH64)

LIBC_INLINE static void inline_memset(Ptr dst, uint8_t value, size_t count) {
#if defined(LIBC_TARGET_ARCH_IS_X86)
  return inline_memset_x86(dst, value, count);
#elif defined(LIBC_TARGET_ARCH_IS_AARCH64)
  return inline_memset_aarch64(dst, value, count);
#elif defined(LIBC_TARGET_ARCH_IS_RISCV64)
  return inline_memset_aligned_access_64bit(dst, value, count);
#elif defined(LIBC_TARGET_ARCH_IS_RISCV32)
  return inline_memset_aligned_access_32bit(dst, value, count);
#else
  return inline_memset_byte_per_byte(dst, value, count);
#endif
}

LIBC_INLINE static void inline_memset(void *dst, uint8_t value, size_t count) {
  inline_memset(reinterpret_cast<Ptr>(dst), value, count);
}

} // namespace __llvm_libc

#endif // LLVM_LIBC_SRC_STRING_MEMORY_UTILS_MEMSET_IMPLEMENTATIONS_H
