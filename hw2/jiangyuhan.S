#define TAR_ADDR 0x50200000

.global main
main:

    li s0, 1
    li s1, 200
    li s2, TAR_ADDR

check_loop:
    mv a0, s0
    call is_prime

    addi s0, s0, 1

    sb a0, 0(s2)
    addi s2, s2, 1   

    ble s0, s1, check_loop
    j end

is_prime:
    mv t0, a0
    li t1, 2

    # n<2
    blt t0, t1, not_prime

    # n=2
    beq t0, t1, is_prime_yes

    # 检查 n 是否为偶数 (n % 2 == 0)
    rem t5, t0, t1
    beqz t5, not_prime

    li t2, 3

is_prime_loop:
    mul t3, t2, t2
    blt t0, t3, is_prime_yes    # if i*i > n, 说明没有因子，是质数

    rem t5, t0, t2              # n % i
    beqz t5, not_prime          # 如果余数为 0，不是质数

    addi t2, t2, 1
    j is_prime_loop

is_prime_yes:
    li a0, 1
    ret

not_prime:
    li a0, 0
    ret

end:
    j end