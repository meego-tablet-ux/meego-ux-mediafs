cmake_minimum_required(VERSION 2.6)
project(meego-ux-mediafs C)

set(CMAKE_MODULE_PATH cmake_modules)

find_package(magic REQUIRED)
find_package(X11 REQUIRED)
find_package(ImageMagick REQUIRED)
find_package(fuse REQUIRED)
find_package(GStreamer REQUIRED)
find_package(GLIB2 REQUIRED)

add_library(thumbnail STATIC thumbnail.c thumbnail.h)
target_link_libraries(thumbnail ${ImageMagick_LIBRARIES} ${GLIB2_LIBRARIES})

add_library(indexer STATIC indexer.c indexer.h)
target_link_libraries(indexer ${ImageMagick_LIBRARIES} ${magic_LIBRARY} ${GLIB2_LIBRARIES})

add_library(mfuse STATIC mfuse.c mfuse.h)
set_target_properties(mfuse PROPERTIES COMPILE_FLAGS "-D_FILE_OFFSET_BITS=64")
target_link_libraries(mfuse ${fuse_LIBRARIES})

add_executable(meego-ux-mediafsd main.c)
set_target_properties(meego-ux-mediafsd PROPERTIES LINK_FLAGS "-ldl")
target_link_libraries(meego-ux-mediafsd mfuse indexer thumbnail)

add_library(plugin-imagemagick SHARED imagemagick.c)
set_target_properties(plugin-imagemagick PROPERTIES COMPILE_FLAGS "-fPIC")
target_link_libraries(plugin-imagemagick ${ImageMagick_LIBRARIES})

add_library(plugin-gstreamer SHARED gstreamer.c)
set_target_properties(plugin-gstreamer PROPERTIES COMPILE_FLAGS "-fPIC")
target_link_libraries(plugin-gstreamer ${GStreamer_LIBRARIES})
