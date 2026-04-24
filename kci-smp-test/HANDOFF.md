# HANDOFF: KCI-SMP-TEST — Dhrystone 기반 캐시 간섭 측정

## 실험 목적

GR740 + RTEMS 6 SMP 환경에서 **dhrystone 워크로드를 수행하는 victim task가 aggressor task의 캐시 간섭을 받을 때 TAT(Turnaround Time)가 어떻게 변화하는지** 측정하고 정량화한다.

`experiments/cache-interference/`의 순수 메모리 sweep 기반 실험에서 한 발 나아가, 현실 응용에 가까운 혼합 연산(분기, 포인터 체이싱, 문자열, 산술)을 수행하는 dhrystone을 victim으로 사용하여 캐시 간섭 취약도를 분석한다.

---

## 프로젝트 구조

```
experiments/kci-smp-test/
├── experiment_common.h           # 공통 헤더 (캐시 파라미터, TAT 통계, workload 함수, dhrystone_context_t)
├── benchmark/
│   ├── dhrystone.h               # dhrystone 타입 + op_type_t + dhrystone_config_t + API 선언
│   ├── dhrystone.c               # context 기반 dhrystone 구현 (전역변수 없음, SMP 안전)
│   └── dhrystone/                # 원본 dhrystone 2.1 소스 (참고용, 실험에서 직접 사용 안 함)
│       ├── dhry.h, dhry_1.c, dhry_2.c
│       └── init.c, README.md
└── exp0-baseline/                # 실험 골격 (현재 stub)
    ├── exp0_baseline.c
    ├── init.c
    └── Makefile
```

---

## 핵심 설계 결정

### 1. Context 기반 dhrystone (전역변수 제거)

원본 `dhry_1.c`/`dhry_2.c`는 전역변수(`Ptr_Glob`, `Int_Glob` 등)를 사용하므로 SMP에서 태스크 간 경쟁이 발생한다. `dhrystone.c`는 모든 상태를 `dhrystone_context_t`에 담아 각 태스크가 독립적인 데이터를 갖도록 구현했다. `Proc_1`~`Proc_8`, `Func_1`~`Func_3`은 context를 인자로 받는 `static` 함수로 재구현되어 있다.

### 2. Victim / Aggressor 역할 분리

| 역할 | 워크로드 | working-set 제어 단위 |
|------|----------|----------------------|
| Victim | dhrystone (`run_dhrystone_workload`) | `Arr_2_Dim` 배열 크기 (NxN) |
| Aggressor | cache sweep (`load/store/rmw_workload`) | `WS_L1_FIT` 등 캐시 단위 상수 |

Victim의 `op_type`은 불필요하다 — dhrystone 자체가 이미 load/store/RMW를 혼합한다.

### 3. `op_type_t`를 `dhrystone.h`로 이동

`experiment_common.h`는 `benchmark/dhrystone.h`를 include하므로, `op_type_t`를 `dhrystone.h`에 두어야 circular include를 피할 수 있다. `experiment_common.h`의 `task_stats_t.op_type`은 이 경로로 공급된다.

### 4. dhrystone의 working-set 확장 방법 (결정, 미구현)

`Arr_2_Dim`은 현재 `int[50][50]` = 10KB로 고정이며 L1에 들어간다. Victim의 working-set을 키우기 위해 **`Arr_2_Dim`을 동적 할당 flat array로 교체**하고, `dhry_proc_8`이 배열 전체를 순회하도록 변경하기로 결정했다.

별도 `working_set_buf`를 두는 방식은 현재 코드에 남아있지만 **폐기 예정**이다 (아래 Next Steps 참고).

---

## 현재 코드 상태 (2026-04-24)

### `benchmark/dhrystone.h`
- `op_type_t` 정의 (LOAD_OP / STORE_OP / RMW_OP) — aggressor용으로 유지
- `dhrystone_config_t`: `arr_dim`, `num_iterations` (victim 필요 필드만 남김)
- `DHRYSTONE_SIZE` = 150 (정적 배열이 최대 arr_dim=150을 수용)
- `dhrystone_init()` / `run_dhrystone_workload()` 선언

### `benchmark/dhrystone.c`
- context 기반 `dhry_func_1~3`, `dhry_proc_1~8` 구현 (원본과 동작 동일, 검증 완료)
- `dhrystone_init`: `ptr_glob`/`next_ptr_glob` malloc, 초기 시드값 설정, `ctx->arr_dim` 세팅
- `run_dhrystone_workload`: `num_iterations`회 dhrystone 루프 수행 (원본 proc_8 그대로)
- `working_set_buf` sweep 로직 제거 완료

### `experiment_common.h`
- `dhrystone_context_t`: `config` + `arr_dim` + dhrystone 데이터 구조
  - `Arr_2_Dim arr_2_glob[150][150]` — 정적 배열, sweep 확장 시 arr_dim까지만 순회
  - `working_set_buf` 제거 완료

### `exp0-baseline/`
- 현재 stub — 실험 로직 없음

---

## 원본과의 의미론적 차이 (검증 완료)

| 항목 | 원본 dhrystone 2.1 | 현 구현 | 영향 |
|------|-------------------|---------|------|
| `Func_1` else 분기 `Ch_1_Glob` 설정 | 있음 | 없음 | 없음 — 표준 입력에서 else 분기 미도달 |
| `Func_2` `strcmp>0` 분기 `Int_Glob` 설정 | 있음 | 없음 | 없음 — "1'ST" < "2'ND"이므로 미도달 |
| `Proc_2` `Enum_Loc` 미초기화 | UB | `Ident_4`로 초기화 | 안전 개선 |
| `Func_2` `Ch_Loc` 미초기화 | UB | `'\0'`으로 초기화 | 안전 개선 |

실행 경로의 동작은 완전히 일치한다.

---

## 실험 시나리오

### 설계 기조

- **Primary axis (aggressor intensity)**: aggressor working-set 크기 변화 → 캐시 간섭 강도
- **Secondary axis (victim memory intensity)**: dhrystone arr_dim 변화 → victim의 캐시 의존도

### 기본 실험: exp1-dhry-vs-sweep

```
Core 0 (victim)    : dhrystone, arr_dim=50 (고정, ~10KB)
Core 1 (aggressor) : cache sweep, WS 변화
```

**Aggressor intensity 축:**

| Aggressor WS | 상수 | 간섭 강도 |
|-------------|------|---------|
| 0 | — | baseline (간섭 없음) |
| ~11.5KB | `WS_L1_FIT` | L1 부분 점유 |
| 32KB | `WS_L1_EXCEED` | L1 완전 thrash |
| 512KB | `WS_L2_QUARTER` | L2 1/4 점유 |
| ~819KB | `WS_L2_PRESSURE` | L2 heavy pressure |

각 WS마다 op_type = LOAD / STORE / RMW 세 가지로 반복.

**측정:** victim TAT (min/avg/max/avg_w/o_loop_overhead), deadline miss ratio

### 확장 실험: exp2-dhry-ws-sensitivity

기본 실험 완료 후, victim arr_dim을 변화시켜 캐시 tier별 간섭 취약도 비교:

| Victim `arr_dim` | WS | 위치 | 예상 특성 |
|-----------------|-----|------|---------|
| 50 | ~10KB | L1 내 | L1 thrash에 민감, L2 간섭엔 둔감 |
| 100 | ~40KB | L2 내 | L2 간섭에 민감 |
| 150 | ~90KB | L2 내 (heavy) | 모든 구간에서 취약 |

각 arr_dim에서 기본 실험과 동일한 aggressor intensity 행렬 반복.

> **구현 참고**: arr_dim > 50일 때 dhrystone 원본 proc_8은 arr_2_glob의 극히 일부(고정 인덱스)만 접근한다. arr_dim에 비례한 working-set 확보가 필요하면 `run_dhrystone_workload` 루프 이후 arr_2_glob 별도 sweep 추가를 검토한다.

---

## Next Steps (우선순위 순)

### 1. exp1 구현 ← 당장

`exp0-baseline/`을 참고해 `exp1-dhry-vs-sweep/` 생성:
- Victim: Core 0, dhrystone (`arr_dim=50`, `num_iterations` 고정)
- Aggressor: Core 1, cache sweep (`WS_*` 변화, `op_type` 변화)
- baseline 측정 포함 (aggressor 없이 victim만 실행)
- 측정: Victim TAT (min/avg/max), deadline miss ratio

### 2. proc_8 sweep 확장 (exp2 준비 시)

victim working-set을 arr_dim에 비례하게 하려면 `run_dhrystone_workload` 루프 **이후** arr_2_glob sweep 추가:

```c
/* dhrystone 루프 완료 후 */
int N = ctx->arr_dim;
for (int row = 0; row < N; row++)
  for (int col = 0; col < N; col += L1_LINE_SIZE / sizeof(int))
    ctx->arr_2_glob[row][col] = 0;
```

- proc_8 원본 유지 (dhrystone 혼합 연산 특성 보존)
- sweep은 사후 별도 단계 — 재현성 및 결정론 문제 없음

### 3. exp2 구현

exp1 결과 확인 후 arr_dim = 50 / 100 / 150으로 victim WS sensitivity 분석.

---

## 참조

- `/workspace/experiments/cache-interference/` — 기존 캐시 간섭 실험 (설계 패턴 참고)
- `/workspace/experiments/cache-interference/HANDOFF.md` — CAAS 한계 논증 배경
- `/workspace/experiments/common.mk` — 공통 빌드 시스템
- GR740 캐시: L1 16KB (write-through, load-on-allocate), L2 2MB (공유), L1 line 32B