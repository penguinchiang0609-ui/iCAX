#pragma once
// Static, double-precision collision core; optional IO/serialization disabled.
#define COAL_DLLAPI
#define COAL_LOCAL
#define COAL_VERSION "3.0.4"
#define COAL_MAJOR_VERSION 3
#define COAL_MINOR_VERSION 0
#define COAL_PATCH_VERSION 4
#define COAL_VERSION_AT_LEAST(major,minor,patch) ((major)<3 || ((major)==3 && ((minor)<0 || ((minor)==0 && (patch)<=4))))
