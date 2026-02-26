# exp4-workingset/Makefile
APP = $(BUILDDIR)/exp4_workingset

INCFLAGS = -I..

APP_C_FILES  = init.c
APP_C_FILES += exp4_workingset.c
APP_OBJS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.o)
APP_DEPS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.d)

include ../../common.mk