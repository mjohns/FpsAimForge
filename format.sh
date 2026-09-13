#!/bin/bash
cd src
find . -type f | grep "\.cc" | grep -v ".pb.cc" | xargs clang-format -i
find . -type f | grep "\.h" | grep -v ".pb.h" | xargs clang-format -i
find . -type f | grep "\.proto" | xargs clang-format -i
