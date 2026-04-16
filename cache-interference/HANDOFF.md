# HANDOFF: Cache Interference Experiments on GR740 / RTEMS SMP

## Goal

GR740 + RTEMS 6 SMP 환경에서 **캐시 간섭이 실시간 태스크 성능에 미치는 영향**을 실험적으로 증명한다. 특히 CAAS(Cache Affinity Aware Scheduling) 프레임워크의 한계를 입증하는 것이 최종 목표이다.

### CAAS란 (SAC '26, Shin et al.)

CAAS는 LLVM IR 기반 정적 reuse-distance 분석으로 태스크별 **Cache Affinity Indicator (CA_i)** 를 산출하고, 이를 기반으로 global/clustered/partitioned 중 최적 스케줄링 아키텍처를 RF 모델로 예측하는 프레임워크이다. Low-CA 태스크는 isolated core에 BFD로, High-CA 태스크는 non-isolated core에 WFD로 분산 배치한다.

### CAAS의 한계 (우리 실험이 입증하려는 것)

CAAS의 CA는 **개별 태스크의 reuse distance**만 분석하므로 다음을 포착하지 못한다:

- **태스크 간 L1 시분할 thrashing**: 같은 코어에서 시분할되는 두 태스크의 합산 working-set이 L1을 초과하면 thrashing 발생 → CA는 개별 WS만 봄
- **L2 coherence 비용**: 다른 코어 간 같은 캐시라인 공유 시 snooping 오버헤드 → CA는 공유 주소 정보 없음
- **False sharing**: 같은 캐시라인의 다른 변수 접근 → CA의 RD로는 구분 불가
- **True sharing**: 생산자-소비자 패턴의 coherence 비용 → CA에 공유 데이터 정보 없음
- **Working-set 겹침**: 합산 WS 기반 판단 필요 → CA는 개별 태스크만 분석
- **Conflict miss**: set-associative 매핑 충돌 → CA의 RD는 fully-associative 가정

**핵심**: 동일한 CA 값을 가진 태스크 쌍이라도 배치(같은 코어 vs 다른 코어)에 따라 CPI가 +30~100% 차이 → 간섭 정보 없이는 최적 배치 불가능

## Current Progress — EXP1 측정 완료 (10차 실험, 2026-03-16)

5개 실험 모두 구현 및 빌드 완료. EXP1 (A-1~A-5) 실제 측정 완료. 각 실험은 독립 디렉터리로 분리, 개별 빌드/실행 가능.

### 프로젝트 구조

```
experiments/cache-interference/
├── experiment_common.h                  # 공통 헤더 (캐시 파라미터, 매크로, cache_workload)
├── exp1-baseline/                       # A-1~A-6: 단독 vs L1/L2/L1+L2 간섭
│   ├── Makefile, init.c, exp1_baseline.c
│   └── b-gr740/exp1_baseline.exe
├── exp2-false-sharing/                  # F-1~F-4: False Sharing
│   ├── Makefile, init.c, exp2_false_sharing.c
│   └── b-gr740/exp2_false_sharing.exe
├── exp3-true-sharing/                   # T-1~T-4: Producer-Consumer
│   ├── Makefile, init.c, exp3_true_sharing.c
│   └── b-gr740/exp3_true_sharing.exe
├── exp4-workingset/                     # W-1~W-6: Working-set 겹침
│   ├── Makefile, init.c, exp4_workingset.c
│   └── b-gr740/exp4_workingset.exe
└── exp5-caas-limitation/                # [현 실험 범위 제외] CAAS 한계 직접 재현 시나리오
    └── (구현 보류 — 현재 실험은 SMP RTEMS 캐시 간섭 정량화에 집중)
```

### 핵심 설계 결정

- **주기 태스크**: 모든 Worker/Interferer는 `rtems_rate_monotonic_*` API 기반 periodic task (TASK_PERIOD_TICKS=100, 10000 iterations)
- **동기화**: `rtems_semaphore` (counting/binary)로 동시 시작 + 완료 대기
- **시분할**: 동일 우선순위(10) + 동일 주기 → 같은 코어에서 RR 스케줄링
- **워크로드**: load / store / RMW 워크로드 분리 (`load_workload`, `store_workload`, `rmw_workload`)
- **루프 오버헤드 제거**: `empty_workload`로 루프 오버헤드를 측정하고 `sum_wo_overhead_ns`에서 차감
- **브레이크포인트**: 각 조건이 독립 함수 (`run_exp1_A1` 등) → laysim에서 `b run_exp1_A1` 가능
- **RTEMS 설정**: EDF_SMP 스케줄러, 4코어, MAXIMUM_PERIODS=8~16

### 빌드 방법

```bash
cd experiments/cache-interference/exp1-baseline && make clean && make
# 공통 빌드 시스템: experiments/common.mk
# 각 Makefile은 INCFLAGS=-I.. 으로 상위의 experiment_common.h 참조
```

### EXP1 시나리오 구성 (10차 실험 기준)

| 시나리오 | Working-set | 태스크 구성 | 연산 종류 | 실험 목적 |
|----------|-------------|-------------|-----------|-----------|
| A-1 Solo | L1 × 70% | Core 1, 단독 | Load | L1 Baseline — 100% L1 Hit, 최고 성능 기준선 |
| A-2 L1 Thrash | L1 × 70% | Core 1, Worker+Interferer RR | Load | L1 시분할 thrashing — 합산 WS > L1 |
| A-3 L2 Solo | L1 × 150% | Core 1, 단독 | Load / Store / RMW | L2 Baseline — L1 capacity miss, zero 버스 경쟁 |
| A-4 L2 Saturation | L1 × 150% | Core 0+1, 독립 배열 | Load / Store / RMW | AHB 버스 대역폭 + Write-Through 버퍼 경쟁 |
| A-5 L2 Thrash | L2 × 70% | Core 0+1, 독립 배열 | Load / Store / RMW | L2 capacity miss → DRAM 병목, WCET 최악 조건 |

### 실험별 CAAS 한계 대응

| 실험 | 조건 | CAAS 한계 |
|------|------|-----------|
| EXP1 A-1 vs A-2 | 단독 vs L1 시분할 | CA 동일, 합산 WS 정보 없음 |
| EXP1 A-3 vs A-4 | 단독 vs 2-Core 버스 경쟁 | CA에 AHB 대역폭 포화 미반영 |
| EXP1 A-3 vs A-5 | 단독 vs L2 Thrash | CA에 합산 L2 footprint 미반영 |
| EXP2 F-1 vs F-2 | False sharing vs 패딩 | RD 동일, CPI 2~10x |
| EXP3 T-1 vs T-2 | 다른 코어 vs 같은 코어 | 공유 주소 정보 없음 |
| EXP4 W-2 vs W-6 | 합산>L1 vs 합산≤L1 | 합산 WS 정보 없음 |

## What Worked

- **디렉터리별 분리**: 각 실험이 독립 Makefile + init.c + source → 개별 빌드/실행
- **`INCFLAGS = -I..`**: common.mk의 변수로 상위 공통 헤더 참조
- **전역 배열 + `__attribute__((aligned(4096)))`**: 캐시 실험에 안정적
- **counting semaphore**: 다수 태스크 완료 대기에 적합
- **load/store/RMW 워크로드 분리**: 연산 종류별 간섭 특성 차이를 명확히 구분
- **루프 오버헤드 측정 및 차감**: `empty_workload` 기반 `loop_overhead_ns` 분리로 순수 메모리 접근 지연 측정 가능

## What Didn't Work

- 초기에 모든 실험을 단일 디렉터리에 배치 → 사용자 요청으로 실험별 디렉터리 분리
- 초기에 일반 태스크로 구성 → 사용자 요청으로 rate monotonic 주기 태스크로 변경
- 초기에 단일 RMW 워크로드만 사용 → load/store/RMW로 분리하여 연산 종류별 측정

## EXP1 측정 결과 요약 (10차 실험, 2026-03-16)

| 시나리오 | Op | avg TAT (us) | time_per_access (ns) | vs A-3 delta |
|----------|----|-------------|----------------------|--------------|
| A-1 Solo | Load | 12 | 35 | — (L1 baseline) |
| A-2 STRESS | Load | 24 | 69 | — (L1 thrash) |
| A-3 Solo | Load | 70 | 69 | baseline |
| A-3 Solo | Store | 21 | 20 | baseline |
| A-3 Solo | RMW | 87 | 85 | baseline |
| A-4 Saturation | Load | 98 | 95 | +40% |
| A-4 Saturation | Store | 22 | 22 | +10% |
| A-4 Saturation | RMW | 127 | 124 | +46% |
| A-5 Thrash | Load | 18161 | 395 | ~259× |
| A-5 Thrash | Store | 12104 | 263 | ~577× |
| A-5 Thrash | RMW | 19922 | 434 | ~229× |

**핵심 관찰**: A-5 TAT가 A-3 대비 수백 배 증가 → L2 capacity miss 시 DRAM 병목이 지배적. A-4에서 STORE 간섭이 가장 작은 것은 write-through 버퍼가 read path 경쟁을 일부 흡수하기 때문.

## Next Steps

1. **EXP1 A-3 코드 정비**: 현재 `run_exp1_A3`는 RMW_OP만 실행 → LOAD/STORE/RMW 모두 실행하도록 수정 (문서 기준과 일치시키기)
2. **EXP2~EXP4 실행 및 측정**: False sharing (exp2), True sharing (exp3), Working-set 겹침 (exp4)
3. **결과 분석**: 동일 n_accesses 기준 op type별 time_per_access 비교 → 간섭 비용 정량화
4. **CAAS 한계 논증**: CA가 동일한 태스크 쌍에서 배치별 TAT 차이를 정리하여 논문 반례 구성

### 참조 문서

- `/workspace/cache-interference-experiments.md` — 실험 설계 전체 문서
- `/workspace/experiments/common.mk` — 공통 빌드 시스템
- `/workspace/experiments/gr740-info/` — GR740 예제 프로젝트
- 첨부 PDF: CAAS 논문 (SAC '26, Shin et al.) — CAAS 프레임워크 원본 (`/workspace/CAAS Cache Affinity Aware Scheduling Framework for RTEMS with Edge Computing Support.pdf`)
