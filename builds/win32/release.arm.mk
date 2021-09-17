
# Autogenerated by psync 1.2.0

SOURCES += \
	src/algorithm/YSort.cpp \
	src/b3/blake3_dispatch.c \
	src/b3/blake3_portable.c \
	src/b3/blake3.c \
	src/bech32/segwit_addr.c \
	src/fse/debug.c \
	src/fse/entropy_common.c \
	src/fse/fse_compress.c \
	src/fse/fse_decompress.c \
	src/fse/hist.c \
	src/io/FileStream.cpp \
	src/main.cpp \
	src/memplot/DbgHelper.cpp \
	src/memplot/MemPhase1.cpp \
	src/memplot/MemPhase2.cpp \
	src/memplot/MemPhase3.cpp \
	src/memplot/MemPhase4.cpp \
	src/memplot/MemPlotter.cpp \
	src/pch.cpp \
	src/platform/win32/FileStream_Win32.cpp \
	src/platform/win32/SysHost_Win32.cpp \
	src/PlotContext.cpp \
	src/PlotWriter.cpp \
	src/pos/chacha8.cpp \
	src/SysHost.cpp \
	src/threading/Semaphore.cpp \
	src/threading/Thread.cpp \
	src/threading/ThreadPool.cpp \
	src/Util.cpp \
	src/util/Log.cpp 

CFLAGS += \
	-D_HAS_STD_BYTE=0 \
	-D_WIN32=1 \
	-DWIN32=1 \
	-DWIN32_LEAN_AND_MEAN=1 \
	-DUNICODE=1 \
	-DNOMINMAX=1 \
	-D_CRT_SECURE_NO_WARNINGS=1 \
	-DNDEBUG=1 \
	-D_NDEBUG=1 \
	-Iinclude \
	-Ilib/include \
	-Ilib/include/relic \
	-Isrc 

