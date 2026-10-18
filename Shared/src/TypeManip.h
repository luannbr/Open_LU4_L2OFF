#pragma once

// TypeManip.h is a dependency of Shared/ThreadSafeLibrary.h in the recovered
// source graph. It exposes Loki's compile-time assertion carrier as an empty
// object because target templates contain that one-byte marker as a real
// member and its size therefore participates in their Win64 layouts.
namespace Loki {

template <int Condition>
struct CompileTimeError;

template <>
struct CompileTimeError<1> {};

} // namespace Loki
