Zsh modules providing zsh↔Python bindings

Former name: zsh/zpython. Module adds command `zpython` that is to be used like 
this:

    zmodload libzpython
    zpython "${PYTHON_CODE}"

. Inside python code you may use `zsh` module: e.g.

    zpython "import zsh; print (dir(zsh))"

. For documentation please refer to either `man zpython` (assuming you have it 
installed) or usual python docstrings attached to `zsh` module methods.

# Building and installation

Zpython supports the following variants of building:

1. If you have installed zsh using something like my ebuild: (minimal variant 
   that installs all necessary headers)

        --- zsh-5.0.7-r2.ebuild	2015-01-17 19:09:47.431912274 +0300
        +++ /usr/portage/app-shells/zsh/zsh-5.0.7-r2.ebuild	2014-12-31 11:06:48.000000000 +0300
        @@ -131,22 +131,6 @@
         		doins ${i}/*
         	done

        -	# install header files
        -	insinto /usr/include/zsh
        -	doins "${S}"/Src/*.epro
        -	doins "${S}"/config.h
        -	for file in "${S}"/Src/{zsh.mdh,*.h} ; do
        -		sed -i 's@\.\./config\.h@config.h@' "$file"
        -		sed -i 's@#\(\s*\)include "\([^"]\+\)"@#\1include <zsh/\2>@' "$file"
        -		doins "$file"
        -	done
        -
         	dodoc ChangeLog* META-FAQ NEWS README config.modules

         	if use doc ; then

    , then all you need is `mkdir build && cd build && cmake .. && make`. The 
    resulting `build/libzpython.so` executable must be placed somewhere where 
    zsh can find it: under any directory listed in `$module_path` array.

    You may use this variant with any zsh assuming you can get relevant 
    `config.h` (note: on debian there is a package that installs zsh headers, 
    but it is completely useless because it does not install `config.h` which 
    contains some necessary defines, neither it does install `zsh.mdh` which 
    includes everything module needs. I can live without `zsh.mdh`, but living 
    without `config.h` requires taking checks from zsh autocrap and rewriting 
    them with cmake without any guarantee that this will continue to work after 
    update due to another `#define` added).
2. If you have built your own zsh version then another variant is available: use

        mkdir build
        cd build
        cmake .. -DZSH_REPOSITORY=/path/to/built/zsh

    . This way zpython will use given repository for building: it will take 
    headers from there and also use zsh executable at that location.

Building requires zsh, CMake 2.8.7 or greater, Python-2 2.6 or greater or 
Python-3 3.2 or greater, yodl 3.\* for building man pages.

Some useful CMake options:

`-DZSH_REPOSITORY=/path/to/zsh/repository`: build using existing zsh repository 
(described above).

`-DPYTHON_LIBRARY=/path/to/libpython.so`: build using given Python library.

`-DPYTHON_INCLUDE_DIR=/path/to/Python/include/directory`: build using given 
Python include directory (i.e. directory where `Python.h` is located).

`-DCMAKE_INSTALL_PREFIX=/usr/local`: installation prefix. Is only used for 
determining where to install manual page. `libzpython.so` installation directory 
is the first directory in zsh `$module_path` variable when zsh is launched using 
`zsh -fc 'echo -n $module_path[1]'`.

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
