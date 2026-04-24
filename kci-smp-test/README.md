# KCI-SMP-TEST: Dhrystone 기반 캐시 간섭 측정

GR740 + RTEMS 6 SMP 환경에서 **dhrystone 워크로드(victim)가 캐시 간섭(aggressor)으로 인해 TAT가 어떻게 변화하는지** 측정하는 실험 프레임워크.

---

## 실험 목표

GR740 + RTEMS 6 SMP 환경에서 캐시 간섭(aggressor)이 dhrystone 태스크(victim)의 TAT와 deadline miss ratio에 미치는 영향을 정량화한다.

**aggressor intensity를 주축**으로 삼고, victim working-set은 고정한 뒤 간섭 강도별 성능 저하를 측정한다.


### 기본 실험 (exp1)

- **Victim**: Core 0, dhrystone (`arr_dim=50`, ~10KB, 고정)
- **Aggressor**: Core 1, cache sweep
- **Aggressor intensity 축**: WS 크기 × op_type

| Aggressor WS | 상수 | 간섭 강도 |
|-------------|------|---------|
| 0 | — | baseline |
| ~11.5KB | `WS_L1_FIT` | L1 부분 점유 |
| 32KB | `WS_L1_EXCEED` | L1 완전 thrash |
| 512KB | `WS_L2_QUARTER` | L2 1/4 점유 |
| ~819KB | `WS_L2_PRESSURE` | L2 heavy pressure |

op_type = LOAD / STORE / RMW

### 확장 실험 (exp2)

victim `arr_dim`을 50 / 100 / 150으로 변화시켜 캐시 tier별 간섭 취약도 비교.

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
  int arr_dim;        /* Arr_2_Dim 한 변의 크기. 0이면 기본값 50 사용 */
  int num_iterations; /* 주기당 dhrystone 루프 반복 횟수 */
} dhrystone_config_t;
```

`arr_dim`이 실제로 사용하는 배열 크기를 결정한다. 정적 배열 `Arr_2_Dim[150][150]`을 공유하며 `arr_dim`까지만 sweep한다.

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

## 빌드

```bash
cd exp0-baseline && make clean && make
# 공통 빌드: ../../common.mk
# INCFLAGS = -I.. 으로 experiment_common.h 참조
# INCFLAGS += -I../benchmark 으로 dhrystone.h 참조
```

---

## Victim working-set 크기와 캐시 위치 (exp2용)

| `arr_dim` | `Arr_2_Dim` sweep 범위 | 캐시 위치 |
|-----------|----------------------|----------|
| 50 (기본) | ~10KB | L1 내부 (L1=16KB) |
| 100 | ~40KB | L1 초과, L2 내부 |
| 150 | ~90KB | L2 내부 |

> exp1에서는 `arr_dim=50` 고정. exp2에서 위 세 값을 순회.

---

## 구현 상태

| 항목 | 상태 |
|------|------|
| `dhrystone.h` 타입 및 API | ✅ 완료 |
| `dhrystone.c` context 기반 구현 | ✅ 완료 (원본 2.1과 의미론 검증 완료) |
| `dhrystone_config_t` 정리 (`op_type`, `working_set_size` 제거) | ✅ 완료 |
| `DHRYSTONE_SIZE` 150 확장 + `arr_dim` 필드 추가 | ✅ 완료 |
| exp1 구현 (`exp1-dhry-vs-sweep/`) | ⬜ 미완료 |
| proc_8 이후 arr_2_glob sweep (exp2 준비) | ⬜ 미완료 |
| exp2 구현 (victim WS sensitivity) | ⬜ 미완료 |

자세한 구현 계획 및 실험 시나리오는 `HANDOFF.md` 참고.