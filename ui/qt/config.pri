# Automatically generated from Makefile.nmake. Edit there, not here.
# qmake apparently requires a three-part numeric VERSION.
PROGRAM_NAME = Wireshark
VERSION = 1.12.3
VERSION_FULL = 1.12.3-Gemik
WTAP_VERSION = 1.12.0
INSTALL_DIR = wireshark-gtk2
WIRESHARK_LIB_DIR = C:/Development/Wireshark-win64-libs-1.12
GLIB_DIR = C:/Development/Wireshark-win64-libs-1.12/gtk2
C_ARES_DIR = C:/Development/Wireshark-win64-libs-1.12/c-ares-1.9.1-1-win64ws
ZLIB_DIR = C:/Development/Wireshark-win64-libs-1.12/zlib125
GNUTLS_DIR = C:/Development/Wireshark-win64-libs-1.12/gnutls-3.2.15-2.9-win64ws
SMI_DIR = C:/Development/Wireshark-win64-libs-1.12/libsmi-svn-40773-win64ws
KFW_DIR = 
LUA_DIR = C:/Development/Wireshark-win64-libs-1.12/lua5.2.3
GEOIP_DIR = C:/Development/Wireshark-win64-libs-1.12/GeoIP-1.5.1-2-win64ws
WINSPARKLE_DIR = C:/Development/Wireshark-win64-libs-1.12/WinSparkle-0.3-44-g2c8d9d3-win64ws

INTL_DLL = libintl-8.dll

guilibsdll = kernel32.lib  ws2_32.lib mswsock.lib advapi32.lib user32.lib gdi32.lib comdlg32.lib winspool.lib

HHC_LIBS = htmlhelp.lib

SH = bash 
PYTHON = "C:\tools\python2\python.exe"

MSVC_VARIANT = MSVC2013
MSVCR_DLL = ""

QMAKE_CFLAGS         *= /DWINPCAP_VERSION=4_1_3 /Zi /W3 /MD /O2 /DWIN32_LEAN_AND_MEAN /DMSC_VER_REQUIRED=1800  /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE -DPSAPI_VERSION=1 /D_ALLOW_KEYWORD_MACROS /DBUILD_WINDOWS /Zo /MP /GS /w34295
# NOMINMAX keeps windows.h from defining "min" and "max" via windef.h.
# This avoids conflicts with the C++ standard library.
QMAKE_CXXFLAGS       *= /DWINPCAP_VERSION=4_1_3 /Zi /W3 /MD /O2 /DWIN32_LEAN_AND_MEAN /DMSC_VER_REQUIRED=1800  /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE -DPSAPI_VERSION=1 /D_ALLOW_KEYWORD_MACROS /DBUILD_WINDOWS /Zo /MP /GS /w34295 /DNOMINMAX
QMAKE_LFLAGS         *= /LARGEADDRESSAWARE /NOLOGO /INCREMENTAL:NO /DEBUG /MACHINE:x64 /DYNAMICBASE /FIXED:no

