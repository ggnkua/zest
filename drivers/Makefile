
#TARGETS=drivers.st
PROGS=extmod.prg

AS=vasmm68k_mot
ASOPT=-m68000 -Ftos -tos-flags=7 -nosym

all: $(TARGETS) $(PROGS) $(BIN)

clean:
	rm -f $(TARGETS) $(PROGS) $(BIN) *.o

drivers.st: $(PROGS)
	dd if=/dev/zero of=$@ count=1640
	mformat -i $@ -a -t 82 -s 10 -h 2
	mmd -i $@ ::auto
	mcopy -i $@ $^ ::auto

%.msa: %.st
	rm -f $@
	hmsa $< || true

%.xz: %
	xz -f9 $<

%.prg: %.s
	$(AS) $(ASOPT) $< -o $@
