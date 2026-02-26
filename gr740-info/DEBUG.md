# GR740 GDB Remote Debugging Guide

## 방법 1: .gdbinit 파일 사용 (가장 간단)

### 1단계: 에뮬레이터 실행 (터미널 1)
```bash
cd /workspace/benchmark/gr740-info
laysim-gr740-dbt-cli -gdb
```

### 2단계: GDB 시작 (터미널 2)
```bash
cd /workspace/benchmark/gr740-info
make debug
```

`.gdbinit` 파일이 자동으로 다음을 실행합니다:
- `target extended-remote localhost:1234`
- `load`
- `monitor gdb postload`

### 3단계: 디버깅
```gdb
(gdb) b Init          # 브레이크포인트 설정
(gdb) c               # continue
(gdb) l               # 소스 코드 보기
(gdb) n               # next
(gdb) s               # step into
(gdb) p variable      # 변수 출력
```

---

## 방법 2: Init에 자동 브레이크포인트

Init 함수에 자동으로 브레이크포인트를 설정하고 실행:

```bash
make debug-init
```

---

## 방법 3: 직접 GDB 스크립트 실행

### gdb-commands.txt 생성
```bash
cat > gdb-commands.txt << 'EOF'
target extended-remote localhost:1234
load
monitor gdb postload
b Init
c
EOF
```

### GDB 실행
```bash
sparc-rtems6-gdb -x gdb-commands.txt b-gr740/app.exe
```

---

## 방법 4: 명령행에서 직접 실행

```bash
sparc-rtems6-gdb \
  -ex "target extended-remote localhost:1234" \
  -ex "load" \
  -ex "monitor gdb postload" \
  -ex "b Init" \
  -ex "c" \
  b-gr740/app.exe
```

---

## 유용한 GDB 명령어

### 브레이크포인트
```gdb
b Init                    # 함수명으로 설정
b main.c:15              # 파일:라인으로 설정
b *0x1258                # 주소로 설정
info breakpoints         # 브레이크포인트 목록
delete 1                 # 브레이크포인트 삭제
```

### 실행 제어
```gdb
c / continue             # 계속 실행
n / next                 # 다음 줄 (함수 안으로 들어가지 않음)
s / step                 # 다음 줄 (함수 안으로 들어감)
finish                   # 현재 함수 끝까지 실행
until 20                 # 20번 줄까지 실행
```

### 정보 확인
```gdb
l / list                 # 소스 코드 보기
p variable               # 변수 값 출력
p/x variable             # 16진수로 출력
info registers           # 레지스터 확인
info threads             # 스레드 목록 (SMP)
backtrace / bt           # 콜 스택
where                    # 현재 위치
```

### 메모리 확인
```gdb
x/10x 0x1000            # 0x1000부터 10개 워드를 16진수로
x/10i $pc               # PC부터 10개 명령어 디스어셈블
disassemble Init        # Init 함수 디스어셈블
```

### SMP 디버깅
```gdb
info threads             # 모든 CPU/스레드 보기
thread 2                 # CPU 1로 전환
thread apply all bt      # 모든 스레드의 백트레이스
```

---

## 문제 해결

### 소스 코드가 안 보일 때
```gdb
(gdb) directory /workspace/benchmark/gr740-info
(gdb) list Init
```

### 에뮬레이터 재시작
```gdb
(gdb) monitor reset
(gdb) load
(gdb) monitor gdb postload
```

### GDB 재연결
```gdb
(gdb) disconnect
(gdb) target extended-remote localhost:1234
```

---

## .gdbinit 보안 경고 해결

GDB가 .gdbinit 로딩을 거부하면:

```bash
# 전역 허용 (권장하지 않음)
echo "set auto-load safe-path /" >> ~/.config/gdb/gdbinit

# 또는 특정 디렉토리만 허용 (권장)
echo "add-auto-load-safe-path /workspace/benchmark" >> ~/.config/gdb/gdbinit
```

---

## 빠른 참조

| 작업 | 명령어 |
|------|--------|
| 에뮬레이터 시작 | `laysim-gr740-dbt-cli -gdb` |
| GDB 시작 | `make debug` |
| Init에 중단점 | `make debug-init` |
| 계속 실행 | `c` |
| 다음 줄 | `n` |
| 함수 안으로 | `s` |
| 소스 보기 | `l` |
| 변수 출력 | `p var` |
| 종료 | `quit` |
