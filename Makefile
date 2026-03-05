cat << 'EOF' > Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fPIE -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -Isrc
LDFLAGS = -nostdlib -static -pie --no-dynamic-linker -z max-page-size=0x1000 -T linker.ld

all: image.iso

kernel.elf: src/kernel.c
	$(CC) $(CFLAGS) -c src/kernel.c -o src/kernel.o
	ld src/kernel.o $(LDFLAGS) -o kernel.elf

image.iso: kernel.elf
	mkdir -p iso_root
	cp kernel.elf limine.conf limine/limine-bios.sys \
	   limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/
	mkdir -p iso_root/EFI/BOOT
	cp limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --protective-msdos-label \
		iso_root -o image.iso
	./limine/limine bios-install image.iso

run: image.iso
	qemu-system-x86_64 -cdrom image.iso

clean:
	rm -rf iso_root kernel.elf image.iso src/*.o
EOF
