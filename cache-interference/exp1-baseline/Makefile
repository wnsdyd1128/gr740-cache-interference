# exp1-baseline/Makefile
APP = $(BUILDDIR)/exp1_baseline

INCFLAGS = -I..

APP_C_FILES  = init.c
APP_C_FILES += exp1_baseline.c
APP_OBJS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.o)
APP_DEPS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.d)

include ../../common.mk