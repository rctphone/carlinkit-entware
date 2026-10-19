# U2W adbd: device daemon from AOSP system/core android-5.0.2, built as plain Linux C.
SRCS += adb.c adb_auth_client.c fdevent.c file_sync_service.c framebuffer_service.c \
	jdwp_service.c remount_service.c services.c sockets.c transport.c \
	transport_local.c transport_usb.c usb_linux_client.c

VPATH += ../libcutils ../libmincrypt u2w
SRCS += socket_inaddr_any_server.c socket_local_client.c socket_local_server.c \
	socket_loopback_client.c socket_loopback_server.c socket_network_client.c
SRCS += rsa.c sha.c sha256.c
SRCS += u2w_properties.c u2w_log.c

override CPPFLAGS += -DADB_HOST=0 -DALLOW_ADBD_ROOT=1 -DHAVE_FORKEXEC=1 -DHAVE_PTHREADS=1
override CPPFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -include grp.h   # bionic declares setgroups() in unistd.h
override CPPFLAGS += -I. -Iu2w/compat -I../include
override CFLAGS += -Wall -Wno-unused-parameter -Wno-deprecated-declarations
override LIBS += -lpthread -lresolv

OBJS = $(notdir $(SRCS:.c=.o))

all: adbd

adbd: $(OBJS)
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $(OBJS) $(LIBS)

clean:
	rm -f adbd $(OBJS)
