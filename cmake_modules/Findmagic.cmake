# - Try to find libmagic header and library
#
# Usage of this module as follows:
#
#     find_package(magic)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  magic_ROOT_DIR         Set this variable to the root installation of
#                            libmagic if the module has problems finding the
#                            proper installation path.
#
# Variables defined by this module:
#
#  LIBMAGIC_FOUND              System has libmagic and magic.h
#  magic_LIBRARY            The libmagic library
#  magic_INCLUDE_DIR        The location of magic.h

find_path(magic_ROOT_DIR
	NAMES include/magic.h
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	# the static version of the library is preferred on OS X for the
	# purposes of making packages (libmagic doesn't ship w/ OS X)
	set(libmagic_names libmagic.a magic)
else ()
	set(libmagic_names magic)
endif ()

find_library(magic_LIBRARY
	NAMES ${libmagic_names}
	HINTS ${magic_ROOT_DIR}/lib
)

find_path(magic_INCLUDE_DIR
	NAMES magic.h
	HINTS ${magic_ROOT_DIR}/include
)

include(FindPackageHandleStandardArgs)
	find_package_handle_standard_args(magic DEFAULT_MSG
	magic_LIBRARY
	magic_INCLUDE_DIR
)

mark_as_advanced(
	magic_ROOT_DIR
	magic_LIBRARY
	magic_INCLUDE_DIR
)
