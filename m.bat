@echo off
del an.exe
del an.pdb
@echo on

cl /nologo an.cxx /MT /Ox /Qpar /O2 /Oi /Ob2 /EHac /Zi /Gy /DNDEBUG /D_AMD64_ /link ntdll.lib /OPT:REF



