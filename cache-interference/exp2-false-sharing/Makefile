# exp2-false-sharing/Makefile
APP = $(BUILDDIR)/exp2_false_sharing

INCFLAGS = -I..

APP_C_FILES  = init.c
APP_C_FILES += exp2_false_sharing.c
APP_OBJS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.o)
APP_DEPS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.d)

include ../../common.mk