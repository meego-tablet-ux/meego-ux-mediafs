find_package(PkgConfig)
pkg_check_modules(fuse fuse)
include_directories(${fuse_INCLUDE_DIRS})
