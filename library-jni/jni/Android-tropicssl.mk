#the tropicssl library
include $(CLEAR_VARS)

LOCAL_CFLAGS :=  -std=gnu99

SRC_FILES := \
	aes.c		arc4.c		base64.c	\
	bignum.c	certs.c		debug.c		\
	des.c		dhm.c		havege.c	\
	md2.c		md4.c		md5.c		\
	net.c		padlock.c	rsa.c		\
	sha1.c		sha2.c		sha4.c		\
	ssl_cli.c	ssl_srv.c	ssl_tls.c	\
	timing.c	x509parse.c	xtea.c		\
	camellia.c
SRC_DIR=tropicssl/library

#disable thumb
LOCAL_ARM_MODE := arm
LOCAL_CFLAGS := -O3
	
LOCAL_C_INCLUDES := $(LOCAL_PATH)/tropicssl/include/
LOCAL_ALLOW_UNDEFINED_SYMBOLS=false
LOCAL_MODULE := tropicssl
LOCAL_SRC_FILES := $(addprefix $(SRC_DIR)/,$(SRC_FILES))

LOCAL_LDLIBS := -ldl -llog 

include $(BUILD_STATIC_LIBRARY)
