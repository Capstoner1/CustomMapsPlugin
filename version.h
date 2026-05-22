#pragma once
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0
#define VERSION_BUILD 45

#define stringify(a) # a
#define expand_and_stringify(a) stringify(a)
#define plugin_version expand_and_stringify(VERSION_MAJOR) "." expand_and_stringify(VERSION_MINOR) "." expand_and_stringify(VERSION_PATCH)
