Zsh modules providing zsh↔Python bindings

Former name: zsh/zpython. Module adds command `zpython` that is to be used like 
this:

    zmodload libzpython
    zpython "${PYTHON_CODE}"

. Inside python code you may use `zsh` module: e.g.

    zpython "import zsh; print (dir(zsh))"

. For documentation please refer to either `man zpython` (assuming you have it 
installed) or usual python docstrings attached to `zsh` module methods.

# Known bugs

Zpython module is known to not support module reloading. This works:

    zsh -c 'zmodload libzpython; zmodload -u libzpython; zmodload libzpython'

. This also works:

    zsh
    % # The following command will load libzpython module and intall powerline prompt
    % . ${PATH_TO_POWERLINE}/powerline/bindings/zsh/powerline.zsh
    > zmodload -u libzpython
    % . ${PATH_TO_POWERLINE}/powerline/bindings/zsh/powerline.zsh

. But if you try to uncomment `zmodload -u libzpython` in provided tests zsh 
will crash.
