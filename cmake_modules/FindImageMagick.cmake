find_package(PkgConfig)
pkg_check_modules(ImageMagick ImageMagick MagickCore)
include_directories(${ImageMagick_INCLUDE_DIRS})
