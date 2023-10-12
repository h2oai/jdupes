cl /DON_WINDOWS /DUNICODE /O2 /W4 /std:c17 /c *.c
link /lib *.obj /out:libjodycode.lib
link /dll *.obj /out:libjodycode.dll