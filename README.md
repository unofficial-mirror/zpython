Zsh modules providing zsh↔Python bindings

Former name: zsh/zpython. Module adds command `zpython` that is to be used like 
this:

    zpython "${PYTHON_CODE}"

. Inside python code you may use `zsh` module: e.g.

    zpython "import zsh; print (dir(zsh))"

. For documentation please refer to either `man zpython` (assuming you have it 
installed) or usual python docstrings attached to `zsh` module methods.
