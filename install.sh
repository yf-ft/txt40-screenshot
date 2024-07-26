#!/bin/bash

# Define variables
TARGET_DIR="/usr/local/bin"
BINARY_NAME="screenshot"
REPO_URL="https://github.com/yf-ft/txt40-screenshot"
BINARY_URL="$REPO_URL/releases/latest/download/$BINARY_NAME"
HEADERS_URL="$REPO_URL/raw/main/libpng-headers"
SOURCE_FILE_URL="$REPO_URL/raw/main/screenshot.cpp"

# Function to check for root privileges
check_root() {
  if [ "$EUID" -ne 0 ]; then
    echo "Please run as root to install to $TARGET_DIR"
    exit 1
  fi
}

# Function to create the target directory if it doesn't exist
create_target_dir() {
  if [ ! -d "$TARGET_DIR" ]; then
    mkdir -p "$TARGET_DIR"
  fi
}

# Function to download the binary
download_binary() {
  echo "Downloading binary..."
  wget -q -O "$BINARY_NAME" "$BINARY_URL"
  if [ $? -ne 0 ]; then
    echo "Failed to download binary."
    exit 1
  fi
  install_binary
}

# Function to build the binary from source
build_binary() {
  echo "Building from source..."
  mkdir -p tmp_build
  cd tmp_build || exit 1

  wget -q "$HEADERS_URL/png.h" -O png.h
  wget -q "$HEADERS_URL/pngconf.h" -O pngconf.h
  wget -q "$HEADERS_URL/pnglibconf.h" -O pnglibconf.h
  wget -q "$SOURCE_FILE_URL" -O screenshot.cpp

  g++ -std=c++17 -O2 -o "$BINARY_NAME" screenshot.cpp -I. /usr/lib/libpng16.so.16.36.0
  if [ $? -ne 0 ]; then
    echo "Failed to build binary."
    exit 1
  fi

  mv "$BINARY_NAME" ..
  cd .. || exit 1
  rm -rf tmp_build
  install_binary
}

# Function to install the binary
install_binary() {
  create_target_dir

  if [ -w "$TARGET_DIR" ]; then
    mv "$BINARY_NAME" "$TARGET_DIR/$BINARY_NAME"
  else
    check_root
    mv "$BINARY_NAME" "$TARGET_DIR/$BINARY_NAME"
  fi

  chmod +x "$TARGET_DIR/$BINARY_NAME"
  if [ "$EUID" -eq 0 ]; then
    chmod u+s "$TARGET_DIR/$BINARY_NAME"
  fi

  echo "Installation complete. You can now use '$BINARY_NAME'."
}

# Parse command line arguments
if [ "$1" == "-b" ] || [ "$1" == "--build" ]; then
  build_binary
else
  download_binary
fi