# GR740 GDB Remote Debugging Configuration
# laysim-gr740-dbt-cli -gdb 실행 후 사용

# 원격 디버깅 연결
target extended-remote localhost:1234

# 프로그램 로드
load

# laysim postload 명령어 (SMP 초기화)
monitor gdb postload

# 유용한 설정
set print pretty on
set pagination off

# Init 함수에 브레이크포인트 설정 (선택 사항)
# b Init

# 시작하려면 'continue' 또는 'c' 입력
echo \n=== GDB Ready ===\n
echo Type 'c' to continue to breakpoint\n
echo Type 'b Init' to set breakpoint at Init\n\n
