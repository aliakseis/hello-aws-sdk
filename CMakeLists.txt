# minimal CMakeLists.txt for the AWS SDK for C++
cmake_minimum_required(VERSION 3.1)

set(CMAKE_CXX_STANDARD 11)

# just an example value.
project(hello-aws-sdk)

# Locate the AWS SDK for C++ package.
# Requires that you build with:
#   -Daws-sdk-cpp_DIR=/path/to/sdk_build
# or export/set:
#   CMAKE_PREFIX_PATH=/path/to/sdk_build
#find_package(aws-sdk-cpp)
find_package(AWSSDK REQUIRED)

# Link to the SDK shared libraries.
add_definitions(-DUSE_IMPORT_EXPORT)

# The executable name and its sourcefiles
add_executable(hello-aws-sdk hello-aws-sdk.cpp)

# The libraries used by your executable.
# "aws-cpp-sdk-s3" is just an example.
target_link_libraries(hello-aws-sdk aws-cpp-sdk-core aws-cpp-sdk-sns aws-cpp-sdk-sqs)

