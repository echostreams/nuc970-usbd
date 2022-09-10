# CMakeList.txt : CMake project for nuc970-usbd, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

project ("nuc970-usbd")

# Add source to this project's executable.
add_executable (nuc970-usbd "nuc970-usbd.c" "usbip.c" "usbd.c")

if(WIN32)

else()
	add_definitions(-DWall -DLINUX)
endif()

# TODO: Add tests and install targets if needed.
