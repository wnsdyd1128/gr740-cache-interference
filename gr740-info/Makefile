# ===== memory_test0/Makefile =====

# 1️⃣ 프로젝트별 설정을 먼저 정의
APP = $(BUILDDIR)/app

APP_C_FILES = init.c
APP_C_FILES += main.c
APP_OBJS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.o)
APP_DEPS = $(APP_C_FILES:%.c=$(BUILDDIR)/%.d)

# 2️⃣ 마지막에 공통 규칙 include
include ../common.mk
