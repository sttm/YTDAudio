#pragma once

// Cross-platform macros for Windows compatibility
// Include this file instead of defining macros in multiple places

#ifdef _WIN32
#include <sys/stat.h>
#include <direct.h>
#include <io.h>

// mkdir compatibility
#ifndef mkdir
#define mkdir(path, mode) _mkdir(path)
#endif

// S_ISDIR compatibility
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

// S_ISREG compatibility
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

// S_IXUSR compatibility
#ifndef S_IXUSR
#define S_IXUSR _S_IEXEC
#endif

// popen/pclose compatibility
#ifndef popen
#define popen _popen
#endif

#ifndef pclose
#define pclose _pclose
#endif

#endif // _WIN32



