import argparse

def generate_single_nop_macro(n: int, output_path: str):
    if n <= 0:
        raise ValueError("n은 1 이상의 정수여야 합니다.")

    header_guard = "__NOP_H__"

    lines = []
    lines.append(f"#ifndef {header_guard}")
    lines.append(f"#define {header_guard}")
    lines.append("")
    lines.append("#include <rtems.h>")
    lines.append("#include <stdint.h>")
    lines.append("#include <stdio.h>")
    lines.append("")
    lines.append("/* 자동 생성된 NOP 매크로 헤더 */")
    lines.append("/* 생성 스크립트: generate_nop_header.py */")
    lines.append(f"/* 생성 대상: NOP_{n} */")
    lines.append("")

    # 기본 NOP_1 정의
    lines.append('#define NOP_1() __asm__ volatile("nop")')

    # 필요한 2^k 매크로 수집
    needed_powers = []
    bit = 1
    temp = n
    while temp > 0:
        if temp & 1:
            needed_powers.append(bit)
        bit <<= 1
        temp >>= 1

    # 최대 필요한 2^k까지 매크로 생성
    max_power = max(needed_powers)
    power = 2
    while power <= max_power:
        prev = power // 2
        lines.append(f"#define NOP_{power}() do {{ NOP_{prev}(); NOP_{prev}(); }} while (0)")
        power *= 2

    # 만약 n이 2의 거듭제곱이면 여기서 종료
    if n & (n - 1) == 0:
        lines.append("")
        lines.append(f"#endif /* {header_guard} */")
        with open(output_path, "w") as f:
            f.write("\n".join(lines))
        print(f"✅ '{output_path}' 파일이 생성되었습니다. (NOP_{n}은 2의 거듭제곱이므로 기본 매크로만 정의됨)")
        return

    # 비트 분해 조합
    parts = needed_powers[::-1]  # 큰 비트부터 실행
    macro_body = "; ".join([f"NOP_{p}()" for p in parts])
    lines.append(f"#define NOP_{n}() do {{ {macro_body}; }} while (0)")

    lines.append("")
    lines.append(f"#endif /* {header_guard} */")

    with open(output_path, "w") as f:
        f.write("\n".join(lines))

    print(f"✅ '{output_path}' 파일이 생성되었습니다.")
    print(f"생성된 매크로: NOP_{n} (필요한 하위 매크로 포함)")


def main():
    parser = argparse.ArgumentParser(
        description="단일 NOP_n 매크로 자동 생성기 (비트 분해 기반)"
    )
    parser.add_argument(
        "-n",
        type=int,
        required=True,
        help="생성할 NOP 매크로의 n 값 (예: 13 → NOP_13())",
    )
    parser.add_argument(
        "-o",
        "--output_path",
        type=str,
        default="nop.h",
        help="출력 파일 경로 (기본값: nop.h)",
    )
    args = parser.parse_args()

    generate_single_nop_macro(args.n, args.output_path)


if __name__ == "__main__":
    try:
        main()
    except ValueError as e:
        print(f"❌ 입력 오류: {e}")
