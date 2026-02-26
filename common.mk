# ===== common.mk =====
# RTEMS 기본 설정
RTEMS_API = 6
RTEMS_CPU = sparc
RTEMS_BSP = gr740

RTEMS_ROOT = /opt/rtems/6
PKG_CONFIG = $(RTEMS_ROOT)/lib/pkgconfig/$(RTEMS_CPU)-rtems$(RTEMS_API)-$(RTEMS_BSP).pc
BUILDDIR = b-$(RTEMS_BSP)

# 공통 컴파일 옵션
DEPFLAGS = -MT $@ -MD -MP -MF $(basename $@).d
WARNFLAGS = -Wall -Wextra
OPTFLAGS = -O0 -g -ffunction-sections -fdata-sections -mcpu=leon3 -mfpu -mhard-float
OPTFLAGS_DEBUG = -O0 -g -ffunction-sections -fdata-sections -mcpu=leon3 -mfpu -mhard-float
EXEEXT = .exe

# CFG 플래그 (제어 흐름 그래프 시각화용)
# -g0 제거: GDB 디버깅을 위해 디버그 심볼 유지 필요
CFG_FLAGS = -fdump-tree-cfg-graph
# CFG_FLAGS += -fdump-tree-gimple

# ABI 및 링크 옵션
ABI_FLAGS = $(shell pkg-config --cflags $(PKG_CONFIG))
LDFLAGS = $(shell pkg-config --libs $(PKG_CONFIG)) -lrtemsbsp -lrtemstest -lrtemscpu -lm

# 기본 include / flags (프로젝트에서 INCFLAGS 추가 가능)
CPPFLAGS = $(CFG_FLAGS)
CFLAGS = $(DEPFLAGS) $(WARNFLAGS) $(ABI_FLAGS) $(OPTFLAGS) $(CFG_FLAGS)
CFLAGS_DEBUG = $(DEPFLAGS) $(WARNFLAGS) $(ABI_FLAGS) $(OPTFLAGS_DEBUG)
CXXFLAGS = $(DEPFLAGS) $(WARNFLAGS) $(ABI_FLAGS) $(OPTFLAGS)
ASFLAGS = $(ABI_FLAGS)


# 링크 명령어
CCLINK = $(CC) $(CFLAGS) -Wl,-Map,$(basename $@).map
CXXLINK = $(CXX) $(CXXFLAGS) -Wl,-Map,$(basename $@).map

# RTEMS 도구체인 경로
export PATH := $(RTEMS_ROOT)/bin:$(PATH)
export AR = $(RTEMS_CPU)-rtems$(RTEMS_API)-ar
export AS = $(RTEMS_CPU)-rtems$(RTEMS_API)-as
export CC = $(RTEMS_CPU)-rtems$(RTEMS_API)-gcc
export CXX = $(RTEMS_CPU)-rtems$(RTEMS_API)-g++
export LD = $(RTEMS_CPU)-rtems$(RTEMS_API)-ld
export NM = $(RTEMS_CPU)-rtems$(RTEMS_API)-nm
export OBJCOPY = $(RTEMS_CPU)-rtems$(RTEMS_API)-objcopy
export RANLIB = $(RTEMS_CPU)-rtems$(RTEMS_API)-ranlib
export SIZE = $(RTEMS_CPU)-rtems$(RTEMS_API)-size
export STRIP = $(RTEMS_CPU)-rtems$(RTEMS_API)-strip

# ===== 공통 빌드 규칙 =====

# C
$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(INCFLAGS) $(CFLAGS) -c $< -o $@

# Assembly (.S)
$(BUILDDIR)/%.o: %.S | $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(INCFLAGS) -DASM $(CFLAGS) -c $< -o $@

# C++
$(BUILDDIR)/%.o: %.cc | $(BUILDDIR)
	$(CXX) $(CPPFLAGS) $(INCFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: %.cpp | $(BUILDDIR)
	$(CXX) $(CPPFLAGS) $(INCFLAGS) $(CXXFLAGS) -c $< -o $@

# Assembly (.s)
$(BUILDDIR)/%.o: %.s | $(BUILDDIR)
	$(AS) $(ASFLAGS) $< -o $@

# 기본 빌드 타겟
all: $(APP)$(EXEEXT)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(APP)$(EXEEXT): $(APP_OBJS)
	$(CCLINK) $^ $(LDFLAGS) -o $@

run: $(APP)$(EXEEXT)
	laysim-gr740-cli -r -core0 $<

# GDB 디버깅 (laysim-gr740-dbt-cli -gdb 실행 후 사용)
debug: $(APP)$(EXEEXT)
	@echo "=== Starting GDB ==="
	@echo "Make sure laysim-gr740-cli -gdb is running first!"
	@echo ""
	sparc-rtems6-gdb -q $<

# GDB 디버깅 (브레이크포인트 자동 설정)
debug-init: $(APP)$(EXEEXT)
	@echo "=== Starting GDB with Init breakpoint ==="
	@echo "Make sure laysim-gr740-dbt-cli -gdb is running first!"
	@echo ""
	sparc-rtems6-gdb -q -ex "b Init" -ex "c" $<

clean:
	rm -rf $(BUILDDIR)

-include $(APP_DEPS)
