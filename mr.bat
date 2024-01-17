@echo off

del an.exe
del an.pdb
REM cl /nologo an.cxx /Ox /Qpar /O2 /Oi /Ob2 /EHac /Zi /Gy /DNDEBUG /D_CRT_NON_CONFORMING_SWPRINTFS /D_CRT_SECURE_NO_DEPRECATE  /D_AMD64_ /link ntdll.lib /OPT:REF

cl /W4 /nologo an.cxx /Ox /Qpar /O2 /Oi /Ob2 /EHac /Zi /Gy /DNDEBUG /D_AMD64_ /link ntdll.lib /OPT:REF


