compile: mngr main
	@echo done

mngr: mngr.c mngr.h defines.h
	clang mngr.c -o mngr

main: main.c defines.h grtp.h
	clang main.c -o main
