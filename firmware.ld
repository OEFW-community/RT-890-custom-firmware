ENTRY(HandlerReset)

_estack = 0x20004000;    /* end of 16K RAM */

_Min_Heap_Size = 0;      /* required amount of heap  */
_Min_Stack_Size = 0x80;  /* required amount of stack */

MEMORY
{
	FLASH (rx)      : ORIGIN = 0x08001400, LENGTH = 59K
	RAM (xrw)       : ORIGIN = 0x20000000, LENGTH = 16K
}

SECTIONS
{
	/* Program code */
	.text :
	{
		. = ALIGN(4);
		*(.text.isr)           /* .text sections of code  */
		*(.text)           /* .text sections of code  */
		*(.text*)          /* .text* sections of code */
		*(.rodata)         /* .rodata sections        */
		*(.rodata*)        /* .rodata* sections       */
		*(.glue_7)         /* Glue arm to thumb code  */
		*(.glue_7t)        /* Glue thumb to arm code  */
		*(.eh_frame)

		KEEP(*(.fini))
		. = ALIGN(4);
		_etext = .;        /* global symbols at end   */
	} >FLASH

	/* Unwind tables pulled from libgcc: placed explicitly so that they stay
	   before the .data load image. */
	.ARM.exidx :
	{
		*(.ARM.exidx*)
	} >FLASH

	/* Used by startup code */

	. = ALIGN(4);

	flash_data_start = .;

	.data :
	{
		. = ALIGN(4);
		sram_data_start = .;
		*(.sramtext)
		*(.srambss)
		*(.data)           /* .data sections              */
		*(.data*)          /* .data* sections             */

		. = ALIGN(4);
		_edata = .;        /* Global symbol at data end   */
	} >RAM AT> FLASH

	sram_data_end = .;

	/* Uninitialized data */
	. = ALIGN(4);
	.bss :
	{
		_sbss = .;         /* Global symbol at bss start */
		__bss_start__ = _sbss;
		*(.bss)
		*(.bss*)
		*(COMMON)

		. = ALIGN(4);
		_ebss = .;         /* Global symbol at bss end */
		__bss_end__ = _ebss;
	} >RAM

	/* Multiboot restore overlay (ENABLE_MULTIBOOT only, empty otherwise).
	   The RAM-resident restore code (.mb_ramfunc, see driver/mb_ram.c) is
	   linked at the address of gFlashBuffer (.flashbuf) but loaded in FLASH.
	   It is copied over gFlashBuffer only right before it runs, so it costs
	   no permanent RAM. .flashbuf is not cleared at startup. */
	.flashbuf ALIGN(ADDR(.bss) + SIZEOF(.bss), 4) (NOLOAD) :
	{
		__flashbuf_start = .;
		KEEP(*(.flashbuf))
		. = ALIGN(4);
		__flashbuf_end = .;
	} >RAM

	.mb_ramfunc __flashbuf_start : AT (LOADADDR(.data) + SIZEOF(.data))
	{
		__mb_ramfunc_start = .;
		KEEP(*mb_ram.o(.mb_ramfunc .mb_ramfunc.*))
		. = ALIGN(4);
		__mb_ramfunc_end = .;
	}

	__mb_ramfunc_load_start = LOADADDR(.mb_ramfunc);
	__mb_ramfunc_size = SIZEOF(.mb_ramfunc);
	_eflash_used = LOADADDR(.mb_ramfunc) + SIZEOF(.mb_ramfunc);

	ASSERT(__mb_ramfunc_size == 0 || ADDR(.mb_ramfunc) == ADDR(.flashbuf),
		"Multiboot RAM code and gFlashBuffer do not share the same address")
	/* The 1 KB sector buffer of the restore routine is at gFlashBuffer + 4 KB
	   (MB_RAM_BUFFER_OFFSET in driver/mb_flash.h). */
	ASSERT(__mb_ramfunc_size <= 0x1000 && (__mb_ramfunc_size == 0 || SIZEOF(.flashbuf) >= 0x1400),
		"Multiboot RAM code or its 1 KB sector buffer do not fit in gFlashBuffer")

	/* Check that there is enough RAM */
	._user_heap_stack :
	{
		. = ALIGN(4);
		. = . + _Min_Heap_Size;
		. = . + _Min_Stack_Size;
		. = ALIGN(4);
	} >RAM

	.fill _eflash_used :
	{
		. = ORIGIN(FLASH) + LENGTH(FLASH) - 1;
		BYTE(0x00)
	} > FLASH
}

