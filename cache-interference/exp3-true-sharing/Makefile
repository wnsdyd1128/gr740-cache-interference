# exp3-true-sharing/Makefile
APP = $(BUILDDIR)/exp3_true_sharing

INCFLAGS = -I..

APP_C_FILES  = init.c
APP_C_FILES += exp3_true_sharing.c
APP_OBJS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.o)
APP_DEPS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.d)

include ../../common.mk