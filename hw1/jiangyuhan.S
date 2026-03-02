#define TAR_ADDR 0x55550000

.global main
main:

// TODO: [p0-task1] Add your code here!

    li t0, 0
    li t1, 1
    li t2, 50

loop:
    // add
    add t0, t0, t1
    add t1, t1, 1
    ble t1, t2, loop    // if not over than 50, loop

    la t3, TAR_ADDR  // t3: target address
    sd t0, 0(t3)        // save
    
end:
	j end