# The network game's build id includes a hash of the simulation's code (docs/multiplayer.md "Games on this network"),
# so two builds with different rules refuse each other at the join. Run as
#   cmake -DENGINE_DIR=<engine folder> -DOUT=<header> -P sim-hash.cmake
# by a build rule that depends on the sources: it runs only when one of them changed, and it leaves the header alone
# when the hash came out the same (no rebuild of what includes it).
# Reproducible: the files in sorted order, named by their path relative to the engine folder, their text without
# carriage returns (a checkout with CRLF gives the same id), no dates, no absolute paths.
file(GLOB_RECURSE _files RELATIVE "${ENGINE_DIR}"
  "${ENGINE_DIR}/sim/*.cpp" "${ENGINE_DIR}/sim/*.hpp" "${ENGINE_DIR}/formats/*.cpp" "${ENGINE_DIR}/formats/*.hpp")
list(SORT _files)
set(_all "")
foreach(_f IN LISTS _files)
  file(READ "${ENGINE_DIR}/${_f}" _text)
  string(REPLACE "\r" "" _text "${_text}")
  string(SHA256 _h "${_text}")
  string(APPEND _all "${_f} ${_h}\n")
endforeach()
string(SHA256 _total "${_all}")
string(SUBSTRING "${_total}" 0 12 _short)
set(_content "// Written by scripts/sim-hash.cmake: the hash of engine/sim and engine/formats (${_short}).\n#pragma once\n#define ANTANTA_SIM_HASH \"${_short}\"\n")
set(_old "")
if(EXISTS "${OUT}")
  file(READ "${OUT}" _old)
endif()
if(NOT "${_old}" STREQUAL "${_content}")
  file(WRITE "${OUT}" "${_content}")
endif()
