# exp5-caas-limitation/Makefile
APP = $(BUILDDIR)/exp5_caas_limitation

INCFLAGS = -I..

APP_C_FILES  = init.c
APP_C_FILES += exp5_caas_limitation.c
APP_OBJS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.o)
APP_DEPS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.d)

include ../../common.mk