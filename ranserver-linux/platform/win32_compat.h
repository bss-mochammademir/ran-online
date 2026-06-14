// platform/win32_compat.h — minimal Win32 type shims for non-Windows builds.
// Start of the "Win32 type abstraction" workstream (DWORD/HANDLE/... -> std).
// Intentionally a SMALL representative subset for the foundation smoke; the real
// port expands it. (Kept minimal here so it also compiles on the macOS host used
// as a fallback build — the Linux target has fewer system-header type clashes.)
//
// COORDINATION WITH ODBC: Microsoft/unixODBC <sqltypes.h> ALSO defines
// WORD/DWORD/BYTE. In any translation unit that uses ODBC, include the ODBC
// headers (<sql.h>/<sqlext.h>) BEFORE this header so __SQLTYPES_H is defined and
// we defer those shared types to ODBC — otherwise: "typedef redefinition with
// different types" (ODBC's DWORD vs ours). Discovered while building this smoke.
#pragma once

#ifndef _WIN32
#include <cstdint>
#include <cstddef>

// Types that ODBC's <sqltypes.h> also provides — defer to ODBC when present.
#ifndef __SQLTYPES_H
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t  BYTE;
#endif

// Types ODBC does not define — provided by the shim.
typedef void*    HANDLE;     // real port: map to fd / custom abstraction
typedef void*    LPVOID;

// Wider integers + string-pointer types used across SigmaCore (evidence:
// SigmaCore/Database/Ado/AdoClass.h — LONGLONG/USHORT/LPCTSTR appear there).
// Verified to coexist with ODBC <sqltypes.h> on Linux.
typedef int64_t      LONGLONG;
typedef uint64_t     ULONGLONG;
typedef uint16_t     USHORT;
typedef const char*  LPCSTR;
typedef char*        LPSTR;
typedef const char*  LPCTSTR;   // ANSI build: TCHAR == char
// NOTE — deferred (not simple typedefs, design decisions for the parameterized-
// command port chip): `__int64`, `GUID`, and `_variant_t`. The COM `_variant_t`
// will be replaced by typed accessors / a tagged value, not a typedef.

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#endif  // !_WIN32
