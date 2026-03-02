clear
make clean
make all
riscv64-unknown-linux-gnu-objdump -S build/main > main.asm
make run-smp
