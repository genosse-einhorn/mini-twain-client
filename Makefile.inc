
$(OBJ_PATH)\$(O)\twain.lib: twain_32.def
	link.exe /LIB /OUT:$@ /DEF:twain_32.def /NODEFAULTLIB /MACHINE:$(CPU) /NOLOGO
