#pragma once
/* -DHOST_PCD_<name>=0|1 per build. */
#define FixedPcdGetBool(x) (HOST_PCD_##x)
