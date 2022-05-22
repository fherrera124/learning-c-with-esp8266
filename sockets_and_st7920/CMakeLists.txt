# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# Getting the components folder located in the parent directory of the current example
get_filename_component(PARENT_DIR $ENV{PROJECT_PATH} DIRECTORY)
set(EXTRA_COMPONENT_DIRS ../${PARENT_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(display-socket)
