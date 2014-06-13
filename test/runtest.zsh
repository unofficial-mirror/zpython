typeset -gr ZSH="$1"  # Path to zsh executable
typeset -gr SRC="$2"  # Source directory
typeset -gr BIN="$3"  # Binary directory
typeset -gr CMD="$4"  # Zpython command name

export ZPYTHON="${CMD}"
export MODPATH="${SRC}/test"

module_path=( ${BIN} ${module_path} )

. $SRC/test/ztst.zsh $SRC/test/V09zpython.ztst
