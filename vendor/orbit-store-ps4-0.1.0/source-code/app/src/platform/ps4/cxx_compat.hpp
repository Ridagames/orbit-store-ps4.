// Orbit Store TV app - libc++ compatibility for the OpenOrbis toolchain.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenOrbis ships an old libc++ whose <cmath> only does `using ::name`,
// i.e. it lacks the C++ float/long-double overloads. Float calls then
// resolve to the C double functions, which both loses precision silently
// and breaks type-deduced uses like std::max(1.0f, std::fabs(x)).
// This header is force-included by tools/build.sh (console build only)
// and supplies those overloads through the musl float builtins.

#pragma once

// Old libc++'s <cmath> does `using ::abs`, which musl only declares via
// <stdlib.h>; pull it in first so the global name exists.
#include <stdlib.h>
#include <cmath>

namespace std {

inline float abs(float value) { return fabsf(value); }
inline float fabs(float value) { return fabsf(value); }
inline float sin(float value) { return sinf(value); }
inline float cos(float value) { return cosf(value); }
inline float tan(float value) { return tanf(value); }
inline float asin(float value) { return asinf(value); }
inline float acos(float value) { return acosf(value); }
inline float atan(float value) { return atanf(value); }
inline float atan2(float y, float x) { return atan2f(y, x); }
inline float exp(float value) { return expf(value); }
inline float log(float value) { return logf(value); }
inline float log2(float value) { return log2f(value); }
inline float log10(float value) { return log10f(value); }
inline float sqrt(float value) { return sqrtf(value); }
inline float pow(float base, float power) { return powf(base, power); }
inline float fmod(float x, float y) { return fmodf(x, y); }
inline float floor(float value) { return floorf(value); }
inline float ceil(float value) { return ceilf(value); }
inline float round(float value) { return roundf(value); }
inline float trunc(float value) { return truncf(value); }
inline float hypot(float x, float y) { return hypotf(x, y); }
inline float copysign(float x, float y) { return copysignf(x, y); }
inline float fmin(float x, float y) { return fminf(x, y); }
inline float fmax(float x, float y) { return fmaxf(x, y); }
inline float ldexp(float value, int exponent) { return ldexpf(value, exponent); }
inline float frexp(float value, int* exponent) { return frexpf(value, exponent); }
inline float modf(float value, float* integral) { return modff(value, integral); }
inline float remainder(float x, float y) { return remainderf(x, y); }
inline float nextafter(float x, float y) { return nextafterf(x, y); }
inline int ilogb(float value) { return ::ilogbf(value); }
// isnan/isinf/isfinite/signbit need no shim: musl defines them as
// per-type static inline functions, so libc++'s `using ::` already
// exports float overloads.

}  // namespace std
