# KCI-SMP-TEST: Dhrystone 기반 캐시 간섭 측정

GR740 + RTEMS 6 SMP 환경에서 **dhrystone 워크로드(victim)가 캐시 간섭(aggressor)으로 인해 TAT가 어떻게 변화하는지** 측정하는 실험 프레임워크.

---

## 실험 목표

- Victim: dhrystone 루프를 주기적으로 수행하는 태스크 (Core 0)
- Aggressor: cache sweep(load/store/rmw)으로 캐시 라인을 evict하는 태스크 (Core 1)
- 측정: Victim TAT의 min/avg/max, 간섭 유무에 따른 성능 저하 정량화

실험 변수:
- **Victim working-set**: `Arr_2_Dim` 크기 (50×50 → 100×100 → 150×150)
- **Aggressor working-set**: `WS_L1_FIT` / `WS_L1_EXCEED` / `WS_L2_QUARTER` / `WS_L2_PRESSURE`
- **Aggressor op type**: LOAD / STORE / RMW

---

## 디렉터리 구조

```
kci-smp-test/
├── experiment_common.h      # 공통 헤더
├── benchmark/
│   ├── dhrystone.h          # 타입 정의, dhrystone_config_t, API
│   ├── dhrystone.c          # context 기반 dhrystone 구현
│   └── dhrystone/           # 원본 dhrystone 2.1 (참고용)
└── exp0-baseline/           # 실험 골격 (stub)
```

---

## 핵심 헤더: `experiment_common.h`

### 하드웨어 파라미터 (GR740)

| 상수 | 값 | 설명 |
|------|-----|------|
| `L1_CACHE_SIZE` | 16KB | L1 per-core, write-through |
| `L1_LINE_SIZE` | 32B | |
| `L2_CACHE_SIZE` | 2MB | 공유 |

### Aggressor working-set 상수

| 상수 | 크기 | 간섭 강도 |
|------|------|---------|
| `WS_L1_FIT` | ~11.5KB | L1 부분 점유 |
| `WS_L1_EXCEED` | 32KB | L1 완전 thrash |
| `WS_L2_QUARTER` | 512KB | L2 1/4 점유 |
| `WS_L2_PRESSURE` | ~819KB | L2 heavy pressure |
| `WS_FULL_SIZE` | 2MB | L2 전체 eviction |

### `task_stats_t`

TAT 측정 결과를 담는 구조체. `record_tat()` / `print_task_stats()`로 사용.

### Workload 함수 (Aggressor용)

```c
load_workload(array, size, sweeps);   /* load-only */
store_workload(array, size, sweeps);  /* store-only */
rmw_workload(array, size, sweeps);    /* read-modify-write */
```

---

## 핵심 헤더: `benchmark/dhrystone.h`

### `dhrystone_config_t`

```c
typedef struct {
  int arr_dim;        /* Arr_2_Dim 한 변의 크기. 50 → ~10KB, 100 → ~40KB, 150 → ~90KB */
  int num_iterations; /* 주기당 dhrystone 루프 반복 횟수 */
} dhrystone_config_t;
```

> **주의**: 현재 코드에는 `op_type`과 `working_set_size`가 남아있으나 **폐기 예정**. Victim에는 불필요하다.

### API

```c
/* ctx->config을 채운 뒤 호출. ptr_glob/next_ptr_glob 할당 및 초기값 설정. */
void dhrystone_init(dhrystone_context_t * ctx);

/* num_iterations회 dhrystone 루프 수행. TAT 측정 대상. */
void run_dhrystone_workload(dhrystone_context_t * ctx);
```

### `dhrystone_context_t` (in `experiment_common.h`)

태스크 하나의 dhrystone 상태를 모두 담는다. SMP에서 각 태스크가 독립 인스턴스를 가지므로 전역변수 경쟁 없음.

---

## 실험 설계 패턴

```c
/* Victim task body */
dhrystone_context_t ctx = {0};
ctx.config.arr_dim        = 100;   /* working-set: ~40KB */
ctx.config.num_iterations = 1000;
dhrystone_init(&ctx);

/* 주기 태스크 루프 */
for (int iter = 0; iter < NUM_TASK_ITERATIONS; iter++) {
  rtems_rate_monotonic_period(period_id, TASK_PERIOD_TICKS);
  uint64_t t0 = rtems_clock_get_uptime_nanoseconds();
  run_dhrystone_workload(&ctx);          /* ← TAT 측정 대상 */
  uint64_t t1 = rtems_clock_get_uptime_nanoseconds();
  record_tat(&stats, t1 - t0);
}

/* Aggressor task body */
static volatile uint8_t aggr_buf[WS_L2_PRESSURE] __attribute__((aligned(4096)));
/* 주기 태스크 루프 */
  rmw_workload(aggr_buf, WS_L2_PRESSURE, 1);  /* ← 캐시 간섭 유발 */
```

---

## Victim working-set 크기와 캐시 위치

| `arr_dim` | `Arr_2_Dim` 크기 | 캐시 위치 |
|-----------|----------------|----------|
| 50 (기본) | ~10KB | L1 내부 (L1=16KB) |
| 100 | ~40KB | L1 초과, L2 내부 |
| 150 | ~90KB | L2 내부 |

---

## 빌드

```bash
cd exp0-baseline && make clean && make
# 공통 빌드: ../../common.mk
# INCFLAGS = -I.. 으로 experiment_common.h 참조
# INCFLAGS += -I../benchmark 으로 dhrystone.h 참조
```

---

## 구현 상태

| 항목 | 상태 |
|------|------|
| `dhrystone.h` 타입 및 API | ✅ 완료 |
| `dhrystone.c` context 기반 구현 | ✅ 완료 (원본 2.1과 의미론 검증 완료) |
| `dhrystone_config_t` 정리 (`op_type` 제거) | ⬜ 미완료 |
| `Arr_2_Dim` 동적 할당 | ⬜ 미완료 |
| `dhry_proc_8` 전체 배열 sweep | ⬜ 미완료 |
| exp1 실험 구현 | ⬜ 미완료 |

자세한 구현 계획은 `HANDOFF.md` 참고.