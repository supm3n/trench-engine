// pl_mpeg's header. It names FILE without including <stdio.h>, so this wrapper includes it first (clang-format would
// otherwise sort <cstdio> after it).
#pragma once

#include <cstdio>

#include <pl_mpeg.h>
