lib_LTLIBRARIES = src/libcstor.la

ACLOCAL_AMFLAGS = -I m4

src_libcstor_la_SOURCES = src/cstor_umain.c src/cstor_ucm.c src/cstor_uqp.c \
			  src/cstor_ucq.c src/cstor_urxq.c src/cstor_usrq.c \
			  src/cstor_uiscsi_ddp.c src/cstor_uevent.c \
			  src/cstor_umem.c
src_libcstor_la_LDFLAGS = -version-info $(CURRENT):$(REVISION):$(AGE) -lpthread
include_HEADERS = src/libcstor.h

EXTRA_DIST = ccan/list.h util/compiler.h util/udma_barrier.h

if RUN_LDCONFIG
install-exec-hook:
	ldconfig

uninstall-hook:
	ldconfig
endif
