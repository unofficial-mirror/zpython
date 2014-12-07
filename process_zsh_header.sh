#!/bin/sh
HEADER="$1"
TARGET="$2"

cp "$HEADER" "$TARGET"
sed -i 's@\.\./config\.h@config.h@' "$TARGET"
sed -i 's@#\(\s*\)include "\([^"]\+\)"@#\1include <zsh/\2>@' "$TARGET"
