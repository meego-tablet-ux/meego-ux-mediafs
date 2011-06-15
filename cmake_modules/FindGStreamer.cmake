find_package(PkgConfig)
pkg_check_modules(GStreamer gstreamer-0.10)
include_directories(${GStreamer_INCLUDE_DIRS})
