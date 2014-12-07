# Locate Zsh executable and library
# This module defines
#  ZSH_FOUND, if false, do not try to use zsh
#  ZSH_EXECUTABLE
#  ZSH_MODULES_OUTPUT_DIR, where to output modules to
#  ZSH_INCLUDE_DIR, where to find zsh.h
#  ZSH_VERSION_STRING, the version of Zsh found

#=============================================================================
# Copyright 2007-2009 Kitware, Inc.
# Modified to support Zsh by Nikolay Pavlov 2014
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================
# (To distribute this file outside of CMake, substitute the full
#  License text for the above reference.)
#
# The required version of Zsh can be specified using the
# standard syntax, e.g. FIND_PACKAGE(Zsh 5.0)
# Otherwise the module will search for any available Zsh implementation

SET(_POSSIBLE_ZSH_INCLUDE include)
SET(_POSSIBLE_ZSH_EXECUTABLE zsh)

# Find the zsh executable
FIND_PROGRAM(ZSH_EXECUTABLE
  NAMES ${_POSSIBLE_ZSH_EXECUTABLE}
)

# Find the zsh header
FIND_PATH(ZSH_INCLUDE_DIR zsh/zsh.h
  PATH_SUFFIXES ${_POSSIBLE_ZSH_INCLUDE}
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt
)

# Determine Zsh version
EXECUTE_PROCESS(
  COMMAND ${ZSH_EXECUTABLE} -fc "echo -n $ZSH_VERSION"
  OUTPUT_VARIABLE ZSH_VERSION_STRING
)

EXECUTE_PROCESS(
  COMMAND ${ZSH_EXECUTABLE} -fc "echo -n $module_path[1]"
  OUTPUT_VARIABLE ZSH_MODULES_OUTPUT_DIR
)

INCLUDE(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set ZSH_FOUND to TRUE if all 
# listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Zsh
                                  REQUIRED_VARS ZSH_EXECUTABLE ZSH_INCLUDE_DIR ZSH_MODULES_OUTPUT_DIR
                                  VERSION_VAR ZSH_VERSION_STRING)

MARK_AS_ADVANCED(ZSH_INCLUDE_DIR ZSH_EXECUTABLE ZSH_MODULES_OUTPUT_DIR)

# vim: ts=2:sts=2:sw=2:et
