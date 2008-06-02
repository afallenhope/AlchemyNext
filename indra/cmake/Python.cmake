# -*- cmake -*-

set(PYTHONINTERP_FOUND)

if (WINDOWS)
  # On Windows, explicitly avoid Cygwin Python.

  find_program(PYTHON_EXECUTABLE
    NAMES python25.exe python23.exe python.exe
    NO_DEFAULT_PATH # added so that cmake does not find cygwin python
    PATHS
    [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\2.5\\InstallPath]
    [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\2.4\\InstallPath]
    [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\2.3\\InstallPath]
    )
elseif (EXISTS /etc/debian_version)
  # On Debian and Ubuntu, avoid Python 2.4 if possible.

  find_program(PYTHON_EXECUTABLE python2.5 python2.3 python PATHS /usr/bin)

  if (PYTHON_EXECUTABLE)
    set(PYTHONINTERP_FOUND ON)
  endif (PYTHON_EXECUTABLE)
else (WINDOWS)
  include(FindPythonInterp)
endif (WINDOWS)

if (NOT PYTHON_EXECUTABLE)
  message(FATAL_ERROR "No Python interpreter found")
endif (NOT PYTHON_EXECUTABLE)

mark_as_advanced(PYTHON_EXECUTABLE)
