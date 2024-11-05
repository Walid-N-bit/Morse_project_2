# invoke SourceDir generated makefile for empty.pem3
empty.pem3: .libraries,empty.pem3
.libraries,empty.pem3: package/cfg/empty_pem3.xdl
	$(MAKE) -f /home/linuxlite/workspace_v10/Morse_project_2/src/makefile.libs

clean::
	$(MAKE) -f /home/linuxlite/workspace_v10/Morse_project_2/src/makefile.libs clean

