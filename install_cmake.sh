#!/bin/bash

echo "Installing CMake"

yum -y install wget
cd /tmp
wget https://cmake.org/files/v3.11/cmake-3.11.2-Linux-x86_64.sh
chmod +x /tmp/cmake-3.11.2-Linux-x86_64.sh
cd /usr
/tmp/cmake-3.11.2-Linux-x86_64.sh --skip-license

echo "CMake Successfully Installed"
