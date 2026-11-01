// The simulation's sin, cos, tan, atan2 and acos (docs/determinism.md "The same maths on every PC"): fdlibm 5.3
// (third_party/fdlibm-5.3, built by CMakeLists.txt as the library `fdlibm`), which uses only + - * / and sqrt and so
// gives the same bits on every PC and in the browser. The C library's own functions take a different path on CPUs with
// FMA3 and differ in the last bit now and then, which would split a network game. The sim never calls <cmath>'s
// transcendental functions; sqrt, floor and fabs are exact everywhere and stay as they are.
#pragma once

extern "C" {
double antanta_fdlibm_sin(double);
double antanta_fdlibm_cos(double);
double antanta_fdlibm_tan(double);
double antanta_fdlibm_ieee754_atan2(double, double);
double antanta_fdlibm_ieee754_acos(double);
}

namespace antanta::sim::dmath {

inline double sin(double x) { return antanta_fdlibm_sin(x); }
inline double cos(double x) { return antanta_fdlibm_cos(x); }
inline double tan(double x) { return antanta_fdlibm_tan(x); }
inline double atan2(double y, double x) { return antanta_fdlibm_ieee754_atan2(y, x); }
inline double acos(double x) { return antanta_fdlibm_ieee754_acos(x); }

}  // namespace antanta::sim::dmath
