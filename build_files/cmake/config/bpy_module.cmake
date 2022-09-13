# SPDX-License-Identifier: GPL-2.0-or-later

# defaults for building blender as a python module 'bpy'
#
# Example usage:
#   cmake -C../blender/build_files/cmake/config/bpy_module.cmake  ../blender
#

set(WITH_PYTHON_MODULE       ON  CACHE BOOL "" FORCE)


# -----------------------------------------------------------------------------
# Installation Configuration.

# install into the systems python dir
set(WITH_INSTALL_PORTABLE    OFF CACHE BOOL "" FORCE)

# There is no point in copying python into Python.
set(WITH_PYTHON_INSTALL      OFF CACHE BOOL "" FORCE)

# Depends on Python install, do this to quiet warning.
set(WITH_DRACO               OFF CACHE BOOL "" FORCE)

if(WIN32)
  set(WITH_WINDOWS_BUNDLE_CRT  OFF CACHE BOOL "" FORCE)
endif()


# -----------------------------------------------------------------------------
# Library Compatibility.

# JEMALLOC does not work with `dlopen()` of Python modules:
# https://github.com/jemalloc/jemalloc/issues/1237
set(WITH_MEM_JEMALLOC        OFF CACHE BOOL "" FORCE)


# -----------------------------------------------------------------------------
# Application Support.

# Not useful to include with the Python module.
# Although a way to extract this from Python could be handle,
# this would be better exposed directly via the Python API.
set(WITH_BLENDER_THUMBNAILER OFF CACHE BOOL "" FORCE)


# -----------------------------------------------------------------------------
# Audio Support.

# Disable audio, its possible some developers may want this but for now disable
# so the Python module doesn't hold the audio device and loads quickly.
set(WITH_AUDASPACE           OFF CACHE BOOL "" FORCE)
set(WITH_JACK                OFF CACHE BOOL "" FORCE)
set(WITH_OPENAL              OFF CACHE BOOL "" FORCE)
set(WITH_SDL                 OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
  set(WITH_PULSEAUDIO          OFF CACHE BOOL "" FORCE)
endif()
if(WIN32)
  set(WITH_WASAPI              OFF CACHE BOOL "" FORCE)
endif()
if(APPLE)
  set(WITH_COREAUDIO           OFF CACHE BOOL "" FORCE)
endif()


# -----------------------------------------------------------------------------
# Input Device Support.

# Other features which are not especially useful as a python module.
set(WITH_INPUT_NDOF          OFF CACHE BOOL "" FORCE)
if(WIN32 OR APPLE)
  set(WITH_INPUT_IME           OFF CACHE BOOL "" FORCE)
endif()


# -----------------------------------------------------------------------------
# Language Support.

set(WITH_INTERNATIONAL       OFF CACHE BOOL "" FORCE)
